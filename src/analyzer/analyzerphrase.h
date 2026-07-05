#pragma once

#include <memory>
#include <vector>

#include "analyzer/analyzer.h"
#include "library/dao/analysisdao.h"
#include "track/phrasesegments.h"

class ClusterMeltSegmenter;
class QSqlDatabase;

// CUSTOM: musical structure ("phrase") analyzer for the RekordboxPi skin's
// phrase bar. Feeds the whole track through qm-dsp's ClusterMeltSegmenter
// (the algorithm behind the QM Vamp Segmenter plugin, already vendored in
// lib/qm-dsp for beat/key detection): hidden-Markov decoding over
// constant-Q timbre features, then histogram clustering, yielding sections
// labelled by similarity cluster. Results are cached in the generic
// `analysis` table (AnalysisDao::TYPE_PHRASE) and republished onto the
// Track on every load, mirroring AnalyzerWaveform's store/reload flow.
class AnalyzerPhrase : public Analyzer {
  public:
    AnalyzerPhrase(
            UserSettingsPointer pConfig,
            const QSqlDatabase& dbConnection);
    ~AnalyzerPhrase() override;

    bool initialize(const AnalyzerTrack& track,
            mixxx::audio::SampleRate sampleRate,
            SINT frameLength) override;
    bool processSamples(const CSAMPLE* pIn, SINT count) override;
    void storeResults(TrackPointer tio) override;
    void cleanup() override;

  private:
    mutable AnalysisDao m_analysisDao;

    std::unique_ptr<ClusterMeltSegmenter> m_pSegmenter;
    std::vector<double> m_monoBuffer;
    size_t m_windowSize;
    size_t m_hopSize;
};
