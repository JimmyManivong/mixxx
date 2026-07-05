#include "analyzer/analyzerphrase.h"

#include <QtDebug>

#include "analyzer/analyzertrack.h"
#include "dsp/segmentation/ClusterMeltSegmenter.h"
#include "track/track.h"

namespace {

const QString kAnalysisVersion = QStringLiteral("phrase-clustermelt-1.0");
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
