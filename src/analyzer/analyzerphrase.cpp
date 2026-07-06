#include "analyzer/analyzerphrase.h"

#include <QtDebug>

#include "analyzer/analyzertrack.h"
#include "dsp/segmentation/ClusterMeltSegmenter.h"
#include "track/track.h"

namespace {

const QString kAnalysisVersion = QStringLiteral("phrase-clustermelt-1.9");

// Low-pass cutoff isolating the kick/bass band for role classification.
constexpr double kBassCutoffHz = 130.0;

// Semantic section roles, stored as PhraseSegment::type and mapped to the
// Rekordbox color scheme by WPhraseBar (red/purple/green/olive/blue).
enum SectionRole {
    kRoleIntro = 0,
    kRoleUp = 1,
    kRoleChorus = 2,
    kRoleDown = 3,
    kRoleOutro = 4,
};
const QString kAnalysisDescription = QStringLiteral(
        "Musical structure segmentation (qm-dsp ClusterMeltSegmenter)");

// How many distinct section families ("phrase types") to cluster into.
// Rekordbox uses ~6 semantic labels (intro/verse/bridge/chorus/outro/up);
// 6 clusters gives a similar visual rhythm on the phrase bar.
constexpr int kNumSegmentTypes = 6;

// Minimum section length, in feature hops (hop = 0.2s): 30 hops = 6s,
// under two 8-bar phrases at 130+ BPM. 40 (8s) missed real 8-16 bar
// changes (a bassless melodic dip stayed glued inside a chorus block);
// the extra noise a finer limit lets through is collapsed onto the 8-bar
// grid by snapToPhraseGrid anyway. The qm default is 20 (4s).
constexpr int kNeighbourhoodLimit = 30;

// Sections shorter than this are segmentation noise, not musical phrases
// (~4 bars at 140 BPM); they get absorbed into the preceding section.
constexpr double kMinSectionSeconds = 7.0;

// Phrase grid resolution: 8 bars in 4/4 = 32 beats. Electronic tracks are
// built on 8-bar phrases, so section changes land on this grid; the raw
// segmenter (0.2s hops, tempo-blind) misses it by a bar or two.
constexpr double kPhraseGridBeats = 32.0;

// Snap every internal section boundary to the nearest 8-bar grid line,
// anchored on the first beat. Boundaries that collapse into the same grid
// cell leave zero-length sections, removed here; equal-type neighbors that
// appear as a result are re-merged by cleanupSections() afterwards.
void snapToPhraseGrid(mixxx::PhraseSegments* pSegments,
        double anchorFrame,
        double phraseFrames) {
    mixxx::PhraseSegments& segments = *pSegments;
    if (segments.size() < 2 || phraseFrames <= 0) {
        return;
    }
    const double totalFrames = segments.last().endFrame;
    for (int i = 1; i < segments.size(); ++i) {
        const double k = std::round(
                (segments[i].startFrame - anchorFrame) / phraseFrames);
        const double snapped = qBound(
                0.0, anchorFrame + k * phraseFrames, totalFrames);
        segments[i - 1].endFrame = snapped;
        segments[i].startFrame = snapped;
    }
    for (int i = segments.size() - 1; i >= 0; --i) {
        if (segments[i].endFrame - segments[i].startFrame < 1.0) {
            segments.removeAt(i);
        }
    }
}

// CHORUS threshold, normalized between the quietest and loudest section
// of the track (0 = quietest, 1 = loudest). Normalizing instead of using
// an absolute ratio keeps the distinction meaningful on loudness-war
// electro where every full section sits within ~90% of the peak RMS.
constexpr double kChorusRmsPos = 0.62;

// Relabel sections as Rekordbox-style roles from per-section loudness
// plus a sequence grammar (each section judged individually - the earlier
// per-cluster labelling let one noisy cluster paint quiet sections green):
//   - RMS >= kChorusRmsRatio * track max  -> CHORUS
//   - otherwise, next section is a CHORUS -> UP   (build into the drop)
//   - otherwise                           -> DOWN (cool-off)
// which yields the musically logical INTRO UP CHORUS DOWN UP CHORUS ...
// OUTRO flow. First and last sections are forced to INTRO/OUTRO.
// Same-role neighbors are intentionally NOT merged: Rekordbox draws
// CHORUS|CHORUS as separate 8-bar blocks, and so do we (dark seams).
// INTRO/OUTRO are capped at 16 bars (2 phrase-grid cells): Rekordbox never
// paints a 32-bar intro, so a longer edge section is split on the grid and
// its inner part keeps the energy-based role. edgeAnchor/phraseFrames come
// from the beatgrid; phraseFrames <= 0 disables the capping (no beatgrid).
void relabelSectionRoles(mixxx::PhraseSegments* pSegments,
        const std::vector<double>& hopEnergies,
        double hopFrames,
        double edgeAnchor,
        double phraseFrames) {
    mixxx::PhraseSegments& segments = *pSegments;
    if (segments.isEmpty() || hopEnergies.empty() || hopFrames <= 0) {
        return;
    }
    const int numHops = static_cast<int>(hopEnergies.size());
    QVector<double> sectionRms(segments.size(), 0.0);
    double maxRms = 0.0;
    for (int i = 0; i < segments.size(); ++i) {
        const int h0 = qBound(0,
                static_cast<int>(segments[i].startFrame / hopFrames),
                numHops - 1);
        const int h1 = qBound(h0 + 1,
                static_cast<int>(segments[i].endFrame / hopFrames),
                numHops);
        double sum = 0.0;
        for (int h = h0; h < h1; ++h) {
            sum += hopEnergies[h];
        }
        sectionRms[i] = std::sqrt(sum / (h1 - h0));
        maxRms = qMax(maxRms, sectionRms[i]);
    }
    if (maxRms <= 0.0) {
        return;
    }
    double minRms = maxRms;
    for (int i = 0; i < segments.size(); ++i) {
        minRms = qMin(minRms, sectionRms[i]);
    }
    const double rmsSpan = maxRms - minRms;
    // Pass 1: loudness picks the choruses (threshold normalized within
    // this track's quiet-to-loud span).
    const double chorusThreshold = minRms + kChorusRmsPos * rmsSpan;
    for (int i = 0; i < segments.size(); ++i) {
        segments[i].type = (rmsSpan > 0.0 && sectionRms[i] >= chorusThreshold)
                ? kRoleChorus
                : kRoleDown;
    }
    // Pass 2 (right to left): a non-chorus directly before a chorus is the
    // build-up. A two-block gap between choruses becomes DOWN then UP.
    for (int i = segments.size() - 2; i >= 0; --i) {
        if (segments[i].type != kRoleChorus &&
                segments[i + 1].type == kRoleChorus) {
            segments[i].type = kRoleUp;
        }
    }
    // Pass 3: a long single build-up (a whole breakdown absorbed into one
    // section) reads wrong as all-purple. Longer than 16 bars, split it on
    // the grid: energy falls first (DOWN), then builds into the drop (UP).
    if (phraseFrames > 0) {
        for (int i = segments.size() - 1; i >= 0; --i) {
            if (segments[i].type != kRoleUp ||
                    segments[i].endFrame - segments[i].startFrame <=
                            2 * phraseFrames) {
                continue;
            }
            const double mid =
                    (segments[i].startFrame + segments[i].endFrame) / 2;
            const double k = std::round((mid - edgeAnchor) / phraseFrames);
            const double splitFrame = edgeAnchor + k * phraseFrames;
            if (splitFrame > segments[i].startFrame + 1.0 &&
                    splitFrame < segments[i].endFrame - 1.0) {
                mixxx::PhraseSegment up = segments[i];
                up.startFrame = splitFrame;
                segments[i].endFrame = splitFrame;
                segments[i].type = kRoleDown;
                segments.insert(i + 1, up);
            }
        }
    }
    // Pass 4: a build often crosses the chorus threshold one cell before
    // the actual drop (rising vocals/synths). If the first 8-bar cell of a
    // chorus is clearly quieter than the rest of that chorus, it is still
    // part of the build: extend the preceding UP over it.
    if (phraseFrames > 0) {
        const auto rmsOver = [&](double f0, double f1) {
            const int h0 = qBound(0,
                    static_cast<int>(f0 / hopFrames),
                    numHops - 1);
            const int h1 = qBound(h0 + 1,
                    static_cast<int>(f1 / hopFrames),
                    numHops);
            double sum = 0.0;
            for (int h = h0; h < h1; ++h) {
                sum += hopEnergies[h];
            }
            return std::sqrt(sum / (h1 - h0));
        };
        for (int i = 0; i + 1 < segments.size(); ++i) {
            mixxx::PhraseSegment& chorus = segments[i + 1];
            if (segments[i].type != kRoleUp ||
                    chorus.type != kRoleChorus ||
                    chorus.endFrame - chorus.startFrame < 2 * phraseFrames) {
                continue;
            }
            const double cellEnd = chorus.startFrame + phraseFrames;
            if (rmsOver(chorus.startFrame, cellEnd) <
                    0.9 * rmsOver(cellEnd, chorus.endFrame)) {
                segments[i].endFrame = cellEnd;
                chorus.startFrame = cellEnd;
            }
        }
        // Symmetric trim: the green must stop on the last kick. If the
        // final 8-bar cell of a chorus has clearly less bass than the rest
        // of it, split that cell off as the start of the cool-off (DOWN).
        for (int i = segments.size() - 1; i >= 0; --i) {
            mixxx::PhraseSegment& chorus = segments[i];
            if (chorus.type != kRoleChorus ||
                    chorus.endFrame - chorus.startFrame < 2 * phraseFrames) {
                continue;
            }
            const double cellStart = chorus.endFrame - phraseFrames;
            if (rmsOver(cellStart, chorus.endFrame) <
                    0.9 * rmsOver(chorus.startFrame, cellStart)) {
                mixxx::PhraseSegment down = chorus;
                down.startFrame = cellStart;
                down.type = kRoleDown;
                chorus.endFrame = cellStart;
                segments.insert(i + 1, down);
            }
        }
    }

    const double edgeCap = phraseFrames * 2; // 16 bars
    // INTRO: first 16 bars only; a longer first section is split on the
    // grid line and its remainder keeps the energy-based role.
    {
        mixxx::PhraseSegment& first = segments.first();
        if (edgeCap > 0 && first.endFrame - first.startFrame > edgeCap * 1.25) {
            mixxx::PhraseSegment rest = first;
            rest.startFrame = edgeAnchor + edgeCap;
            first.endFrame = rest.startFrame;
            first.type = kRoleIntro;
            segments.insert(1, rest);
        } else {
            first.type = kRoleIntro;
        }
    }
    // OUTRO: last 16 bars only, split point aligned on the phrase grid.
    if (segments.size() > 1) {
        mixxx::PhraseSegment& last = segments.last();
        if (edgeCap > 0 && last.endFrame - last.startFrame > edgeCap * 1.25) {
            const double k = std::floor(
                    (last.endFrame - edgeCap - edgeAnchor) / phraseFrames);
            const double splitFrame = edgeAnchor + k * phraseFrames;
            if (splitFrame > last.startFrame + 1.0) {
                mixxx::PhraseSegment outro = last;
                outro.startFrame = splitFrame;
                outro.type = kRoleOutro;
                last.endFrame = splitFrame;
                segments.append(outro);
            } else {
                last.type = kRoleOutro;
            }
        } else {
            last.type = kRoleOutro;
        }
    }
}

// Drop sliver sections and re-join equal-type neighbors so the phrase bar
// shows musically plausible blocks instead of 1-2px noise slices.
void cleanupSections(mixxx::PhraseSegments* pSegments, double minFrames) {
    mixxx::PhraseSegments& segments = *pSegments;
    bool changed = true;
    while (changed && segments.size() > 1) {
        changed = false;
        // Merge adjacent sections of the same type.
        for (int i = segments.size() - 1; i > 0; --i) {
            if (segments[i].type == segments[i - 1].type) {
                segments[i - 1].endFrame = segments[i].endFrame;
                segments.removeAt(i);
                changed = true;
            }
        }
        // Absorb the first too-short section found, then re-loop, since
        // absorbing can create new same-type adjacencies.
        for (int i = 0; i < segments.size() && segments.size() > 1; ++i) {
            if (segments[i].endFrame - segments[i].startFrame < minFrames) {
                if (i > 0) {
                    segments[i - 1].endFrame = segments[i].endFrame;
                } else {
                    segments[1].startFrame = segments[0].startFrame;
                }
                segments.removeAt(i);
                changed = true;
                break;
            }
        }
    }
}

} // namespace

AnalyzerPhrase::AnalyzerPhrase(
        UserSettingsPointer pConfig,
        const QSqlDatabase& dbConnection)
        : m_analysisDao(pConfig),
          m_windowSize(0),
          m_hopSize(0) {
    m_analysisDao.initialize(dbConnection);
}

AnalyzerPhrase::~AnalyzerPhrase() = default;

bool AnalyzerPhrase::initialize(const AnalyzerTrack& track,
        mixxx::audio::SampleRate sampleRate,
        SINT frameLength) {
    if (frameLength <= 0) {
        return false;
    }

    const TrackPointer& pTrack = track.getTrack();
    const TrackId trackId = pTrack->getId();
    if (trackId.isValid()) {
        const QList<AnalysisDao::AnalysisInfo> analyses =
                m_analysisDao.getAnalysesForTrackByType(
                        trackId, AnalysisDao::TYPE_PHRASE);
        for (const AnalysisDao::AnalysisInfo& analysis : analyses) {
            if (analysis.version == kAnalysisVersion) {
                const mixxx::PhraseSegments segments =
                        mixxx::deserializePhraseSegments(analysis.data);
                if (!segments.isEmpty()) {
                    // Stored result is still valid: republish it onto the
                    // track for the UI and skip the (expensive) analysis.
                    pTrack->setPhraseSegments(segments);
                    return false;
                }
            }
            // Outdated version or corrupt blob: drop it and re-analyze.
            m_analysisDao.deleteAnalysis(analysis.analysisId);
        }
    }

    ClusterMeltSegmenterParams params;
    params.nclusters = kNumSegmentTypes;
    params.neighbourhoodLimit = kNeighbourhoodLimit;
    m_pSegmenter = std::make_unique<ClusterMeltSegmenter>(params);
    m_pSegmenter->initialise(sampleRate);
    m_sampleRate = sampleRate;
    m_windowSize = static_cast<size_t>(m_pSegmenter->getWindowsize());
    m_hopSize = static_cast<size_t>(m_pSegmenter->getHopsize());
    if (m_windowSize == 0 || m_hopSize == 0) {
        m_pSegmenter.reset();
        return false;
    }
    m_monoBuffer.clear();
    m_monoBuffer.reserve(m_windowSize + m_hopSize);
    m_hopEnergies.clear();
    m_lpState = 0.0;
    m_lpAlpha = 1.0 -
            std::exp(-2.0 * M_PI * kBassCutoffHz / static_cast<double>(sampleRate));
    return true;
}

bool AnalyzerPhrase::processSamples(const CSAMPLE* pIn, SINT count) {
    if (!m_pSegmenter) {
        return false;
    }
    // Analysis buffers are interleaved stereo; downmix to mono doubles.
    const SINT frames = count / 2;
    for (SINT i = 0; i < frames; ++i) {
        m_monoBuffer.push_back(
                0.5 * (static_cast<double>(pIn[i * 2]) +
                        static_cast<double>(pIn[i * 2 + 1])));
    }
    // Slide the feature window over the accumulated audio.
    while (m_monoBuffer.size() >= m_windowSize) {
        m_pSegmenter->extractFeatures(
                m_monoBuffer.data(), static_cast<int>(m_windowSize));
        // Mean-square BASS loudness of the consumed hop (one-pole low-pass
        // keeps the kick band), for section roles in storeResults():
        // chorus = kick present, breakdown/build = kick absent.
        double energy = 0.0;
        for (size_t j = 0; j < m_hopSize; ++j) {
            m_lpState += m_lpAlpha * (m_monoBuffer[j] - m_lpState);
            energy += m_lpState * m_lpState;
        }
        m_hopEnergies.push_back(energy / static_cast<double>(m_hopSize));
        m_monoBuffer.erase(
                m_monoBuffer.begin(),
                m_monoBuffer.begin() + static_cast<long>(m_hopSize));
    }
    return true;
}

void AnalyzerPhrase::storeResults(TrackPointer tio) {
    if (!m_pSegmenter) {
        return;
    }
    m_pSegmenter->segment();
    const Segmentation& segmentation = m_pSegmenter->getSegmentation();

    mixxx::PhraseSegments segments;
    segments.reserve(static_cast<int>(segmentation.segments.size()));
    for (const Segment& segment : segmentation.segments) {
        mixxx::PhraseSegment phraseSegment;
        // qm-dsp reports positions in mono samples at the analysis sample
        // rate, which equals audio frames.
        phraseSegment.startFrame = static_cast<double>(segment.start);
        phraseSegment.endFrame = static_cast<double>(segment.end);
        phraseSegment.type = segment.type;
        segments.append(phraseSegment);
    }
    if (segments.isEmpty()) {
        qWarning() << "AnalyzerPhrase: segmentation produced no segments";
        return;
    }
    // Quantize boundaries to the musical 8-bar grid when a beatgrid is
    // available (AnalyzerBeats runs before us, so fresh scans have one too).
    const mixxx::BeatsPointer pBeats = tio->getBeats();
    double gridAnchor = 0.0;
    double phraseFrames = 0.0;
    if (pBeats) {
        const mixxx::audio::FramePos anchor = pBeats->firstBeat();
        const mixxx::Bpm bpm = pBeats->getBpmInRange(
                mixxx::audio::kStartFramePos,
                mixxx::audio::FramePos(segments.last().endFrame));
        if (anchor.isValid() && bpm.isValid()) {
            const double framesPerBeat = m_sampleRate * 60.0 / bpm.value();
            gridAnchor = anchor.value();
            phraseFrames = framesPerBeat * kPhraseGridBeats;
            snapToPhraseGrid(&segments, gridAnchor, phraseFrames);
        }
    }
    if (phraseFrames <= 0) {
        // No beatgrid: fall back to duration-based noise removal. When
        // snapped, sliver sections already collapsed onto the grid and
        // same-role 8-bar blocks stay split on purpose (Rekordbox look).
        cleanupSections(&segments, kMinSectionSeconds * m_sampleRate);
    }
    relabelSectionRoles(&segments,
            m_hopEnergies,
            static_cast<double>(m_hopSize),
            gridAnchor,
            phraseFrames);

    tio->setPhraseSegments(segments);

    const TrackId trackId = tio->getId();
    if (trackId.isValid()) {
        AnalysisDao::AnalysisInfo analysis;
        analysis.trackId = trackId;
        analysis.type = AnalysisDao::TYPE_PHRASE;
        analysis.description = kAnalysisDescription;
        analysis.version = kAnalysisVersion;
        analysis.data = mixxx::serializePhraseSegments(segments);
        m_analysisDao.saveAnalysis(&analysis);
    }
    qDebug() << "AnalyzerPhrase: stored" << segments.size()
             << "sections for track" << trackId;
}

void AnalyzerPhrase::cleanup() {
    m_pSegmenter.reset();
    m_monoBuffer.clear();
    m_monoBuffer.shrink_to_fit();
    m_hopEnergies.clear();
    m_hopEnergies.shrink_to_fit();
}
