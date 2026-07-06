#include "analyzer/analyzerphrase.h"

#include <QtDebug>
#include <algorithm>
#include <cmath>

#include "analyzer/analyzertrack.h"
#include "track/beats.h"
#include "track/track.h"

namespace {

const QString kAnalysisVersion = QStringLiteral("phrase-bassgate-2.0");
const QString kAnalysisDescription = QStringLiteral(
        "Musical structure sections from beat-aligned bass gating");

// Semantic section roles, painted by WPhraseBar with the Rekordbox
// palette (red/purple/green/brown/blue).
enum SectionRole {
    kRoleIntro = 0,
    kRoleUp = 1,
    kRoleChorus = 2,
    kRoleDown = 3,
    kRoleOutro = 4,
};

// Analysis hop of the bass-energy envelope.
constexpr double kHopSeconds = 0.2;

// Low-pass cutoff isolating the kick/bass band.
constexpr double kBassCutoffHz = 130.0;

// A bar is "kicked" when its bass RMS clears this point between the
// track's quiet bars (20th percentile) and loud bars (90th percentile).
constexpr double kKickGatePos = 0.5;

// Kick gaps up to this many bars stay inside a chorus (edits/fills)...
constexpr int kMaxHoleBars = 2;
// ...and kicked runs shorter than this are fills, not choruses.
constexpr int kMinChorusBars = 4;

// Rekordbox look: long sections split into 8-bar blocks, INTRO/OUTRO
// span exactly the first/last 16 bars.
constexpr int kBlockBars = 8;
constexpr int kEdgeBars = 16;

// Bass RMS over an audio frame range, from the per-hop mean-square table.
double rmsOverRange(const std::vector<double>& hopEnergies,
        double hopFrames,
        double f0,
        double f1) {
    const int numHops = static_cast<int>(hopEnergies.size());
    const int h0 = qBound(0, static_cast<int>(f0 / hopFrames), numHops - 1);
    const int h1 = qBound(h0 + 1, static_cast<int>(f1 / hopFrames), numHops);
    double sum = 0.0;
    for (int h = h0; h < h1; ++h) {
        sum += hopEnergies[h];
    }
    return std::sqrt(sum / (h1 - h0));
}

mixxx::PhraseSegments buildSections(const std::vector<double>& hopEnergies,
        double hopFrames,
        double anchorFrame,
        double barFrames,
        double totalFrames) {
    mixxx::PhraseSegments segments;
    const int nBars =
            static_cast<int>((totalFrames - anchorFrame) / barFrames);
    if (nBars < 2 * kBlockBars || hopEnergies.empty()) {
        return segments;
    }

    // Per-bar bass loudness, gated into kick / no-kick.
    QVector<double> barRms(nBars);
    for (int i = 0; i < nBars; ++i) {
        barRms[i] = rmsOverRange(hopEnergies,
                hopFrames,
                anchorFrame + i * barFrames,
                anchorFrame + (i + 1) * barFrames);
    }
    QVector<double> sorted = barRms;
    std::sort(sorted.begin(), sorted.end());
    const double lo = sorted[nBars / 5];
    const double hi = sorted[(nBars * 9) / 10];
    if (hi <= 0.0) {
        return segments;
    }
    const double threshold = lo + kKickGatePos * (hi - lo);
    QVector<bool> kicked(nBars);
    for (int i = 0; i < nBars; ++i) {
        kicked[i] = barRms[i] >= threshold;
    }
    // Short kickless holes stay inside the surrounding chorus.
    for (int i = 1; i < nBars;) {
        if (!kicked[i] && kicked[i - 1]) {
            int j = i;
            while (j < nBars && !kicked[j]) {
                ++j;
            }
            if (j < nBars && j - i <= kMaxHoleBars) {
                for (int k = i; k < j; ++k) {
                    kicked[k] = true;
                }
            }
            i = j;
        } else {
            ++i;
        }
    }
    // Kicked runs too short to be a chorus are fills.
    for (int i = 0; i < nBars;) {
        if (kicked[i]) {
            int j = i;
            while (j < nBars && kicked[j]) {
                ++j;
            }
            if (j - i < kMinChorusBars) {
                for (int k = i; k < j; ++k) {
                    kicked[k] = false;
                }
            }
            i = j;
        } else {
            ++i;
        }
    }

    // Track edges swallow the pre-anchor lead-in and final partial bar.
    const auto frameAt = [&](int bar) {
        if (bar <= 0) {
            return 0.0;
        }
        if (bar >= nBars) {
            return totalFrames;
        }
        return anchorFrame + bar * barFrames;
    };
    const auto addBlocks = [&](int b0, int b1, int role) {
        while (b0 < b1) {
            // Avoid a trailing sliver: fold a short remainder into the
            // last block instead of emitting a tiny extra one.
            int next = (b1 - b0 < kBlockBars * 3 / 2) ? b1 : b0 + kBlockBars;
            mixxx::PhraseSegment s;
            s.startFrame = frameAt(b0);
            s.endFrame = frameAt(next);
            s.type = role;
            segments.append(s);
            b0 = next;
        }
    };
    // A kickless gap cools off (DOWN) then builds into the next drop
    // (UP); short gaps are all build.
    const auto addGap = [&](int b0, int b1) {
        const int len = b1 - b0;
        if (len <= 0) {
            return;
        }
        if (len <= kBlockBars) {
            addBlocks(b0, b1, kRoleUp);
            return;
        }
        // Split at the (4-bar rounded) middle.
        const int mid = b0 + ((len / 2 + 2) / 4) * 4;
        addBlocks(b0, qMin(mid, b1), kRoleDown);
        addBlocks(qMin(mid, b1), b1, kRoleUp);
    };

    // Chorus runs from the gate.
    QVector<QPair<int, int>> runs;
    for (int i = 0; i < nBars;) {
        if (kicked[i]) {
            int j = i;
            while (j < nBars && kicked[j]) {
                ++j;
            }
            runs.append({i, j});
            i = j;
        } else {
            ++i;
        }
    }

    // INTRO is always exactly the first kEdgeBars bars, whatever plays
    // there (Rekordbox labels the track head INTRO even under a kick).
    const int introEnd = qMin(kEdgeBars, nBars / 2);
    {
        mixxx::PhraseSegment intro;
        intro.startFrame = 0.0;
        intro.endFrame = frameAt(introEnd);
        intro.type = kRoleIntro;
        segments.append(intro);
    }
    const int outroStart = qMax(introEnd, nBars - kEdgeBars);

    int cursor = introEnd;
    for (const auto& run : runs) {
        const int start = qBound(cursor, run.first, outroStart);
        const int end = qBound(cursor, run.second, outroStart);
        addGap(cursor, start);
        addBlocks(start, end, kRoleChorus);
        cursor = qMax(cursor, end);
    }
    if (cursor < outroStart) {
        // Tail gap before the outro is a cool-off, not a build.
        addBlocks(cursor, outroStart, kRoleDown);
    }
    {
        mixxx::PhraseSegment outro;
        outro.startFrame = frameAt(outroStart);
        outro.endFrame = totalFrames;
        outro.type = kRoleOutro;
        segments.append(outro);
    }
    return segments;
}

} // namespace

AnalyzerPhrase::AnalyzerPhrase(
        UserSettingsPointer pConfig,
        const QSqlDatabase& dbConnection)
        : m_analysisDao(pConfig),
          m_hopSize(0),
          m_hopFill(0),
          m_hopAccum(0.0),
          m_lpState(0.0),
          m_lpAlpha(0.0),
          m_active(false) {
    m_analysisDao.initialize(dbConnection);
}

AnalyzerPhrase::~AnalyzerPhrase() = default;

bool AnalyzerPhrase::initialize(const AnalyzerTrack& track,
        mixxx::audio::SampleRate sampleRate,
        SINT frameLength) {
    m_active = false;
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
                    // track for the UI and skip the analysis.
                    pTrack->setPhraseSegments(segments);
                    return false;
                }
            }
            // Outdated version or corrupt blob: drop it and re-analyze.
            m_analysisDao.deleteAnalysis(analysis.analysisId);
        }
    }

    m_hopSize = static_cast<size_t>(sampleRate * kHopSeconds);
    if (m_hopSize == 0) {
        return false;
    }
    m_sampleRate = sampleRate;
    m_hopEnergies.clear();
    m_hopFill = 0;
    m_hopAccum = 0.0;
    m_lpState = 0.0;
    m_lpAlpha = 1.0 -
            std::exp(-2.0 * M_PI * kBassCutoffHz /
                    static_cast<double>(sampleRate));
    m_active = true;
    return true;
}

bool AnalyzerPhrase::processSamples(const CSAMPLE* pIn, SINT count) {
    if (!m_active) {
        return false;
    }
    // Interleaved stereo in: mono-sum, low-pass to the kick band, and
    // accumulate mean-square energy per fixed hop.
    const SINT frames = count / 2;
    for (SINT i = 0; i < frames; ++i) {
        const double mono = 0.5 *
                (static_cast<double>(pIn[i * 2]) +
                        static_cast<double>(pIn[i * 2 + 1]));
        m_lpState += m_lpAlpha * (mono - m_lpState);
        m_hopAccum += m_lpState * m_lpState;
        if (++m_hopFill == m_hopSize) {
            m_hopEnergies.push_back(
                    m_hopAccum / static_cast<double>(m_hopSize));
            m_hopFill = 0;
            m_hopAccum = 0.0;
        }
    }
    return true;
}

void AnalyzerPhrase::storeResults(TrackPointer tio) {
    if (!m_active || m_hopEnergies.empty()) {
        return;
    }
    const double hopFrames = static_cast<double>(m_hopSize);
    const double totalFrames = hopFrames * m_hopEnergies.size();

    // Bar grid straight from the beatgrid (AnalyzerBeats runs first, so
    // fresh scans have one too). No grid, no phrase bar - kick gating
    // without bars would just be guessing.
    const mixxx::BeatsPointer pBeats = tio->getBeats();
    if (!pBeats) {
        qWarning() << "AnalyzerPhrase: no beatgrid, skipping phrase analysis";
        return;
    }
    const mixxx::audio::FramePos anchor = pBeats->firstBeat();
    const mixxx::Bpm bpm = pBeats->getBpmInRange(
            mixxx::audio::kStartFramePos,
            mixxx::audio::FramePos(totalFrames));
    if (!anchor.isValid() || !bpm.isValid()) {
        qWarning() << "AnalyzerPhrase: invalid beatgrid, skipping";
        return;
    }
    const double barFrames = 4.0 * m_sampleRate * 60.0 / bpm.value();

    mixxx::PhraseSegments segments = buildSections(m_hopEnergies,
            hopFrames,
            anchor.value(),
            barFrames,
            totalFrames);
    if (segments.isEmpty()) {
        qWarning() << "AnalyzerPhrase: track too short for sections";
        return;
    }

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
    m_active = false;
    m_hopEnergies.clear();
    m_hopEnergies.shrink_to_fit();
}
