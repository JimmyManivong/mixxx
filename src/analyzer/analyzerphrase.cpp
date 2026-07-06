#include "analyzer/analyzerphrase.h"

#include <QtDebug>

#include "analyzer/analyzertrack.h"
#include "dsp/segmentation/ClusterMeltSegmenter.h"
#include "track/track.h"

namespace {

const QString kAnalysisVersion = QStringLiteral("phrase-clustermelt-1.2");
const QString kAnalysisDescription = QStringLiteral(
        "Musical structure segmentation (qm-dsp ClusterMeltSegmenter)");

// How many distinct section families ("phrase types") to cluster into.
// Rekordbox uses ~6 semantic labels (intro/verse/bridge/chorus/outro/up);
// 6 clusters gives a similar visual rhythm on the phrase bar.
constexpr int kNumSegmentTypes = 6;

// Minimum section length, in feature hops (hop = 0.2s): 40 hops = 8s,
// roughly a 4-bar phrase at 120-150 BPM. The qm default (20 hops = 4s)
// produces choppier sections than the Rekordbox look wants.
constexpr int kNeighbourhoodLimit = 40;

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
    if (pBeats) {
        const mixxx::audio::FramePos anchor = pBeats->firstBeat();
        const mixxx::Bpm bpm = pBeats->getBpmInRange(
                mixxx::audio::kStartFramePos,
                mixxx::audio::FramePos(segments.last().endFrame));
        if (anchor.isValid() && bpm.isValid()) {
            const double framesPerBeat = m_sampleRate * 60.0 / bpm.value();
            snapToPhraseGrid(&segments,
                    anchor.value(),
                    framesPerBeat * kPhraseGridBeats);
        }
    }
    cleanupSections(&segments, kMinSectionSeconds * m_sampleRate);

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
}
