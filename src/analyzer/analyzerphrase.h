#pragma once

#include <vector>

#include "analyzer/analyzer.h"
#include "library/dao/analysisdao.h"
#include "track/phrasesegments.h"

class QSqlDatabase;

// CUSTOM: musical structure ("phrase") analyzer for the RekordboxPi skin's
// phrase bar. Sections are built directly from the kick: a one-pole
// low-pass isolates the bass band, its energy is measured per beatgrid
// bar, and bars are gated into kick/no-kick. Chorus sections are the
// kicked runs - green starts exactly on the bar the kick appears and ends
// on the bar it stops, hugging the solid blue rectangles of the overview
// waveform like Rekordbox. INTRO/OUTRO are the fixed track edges, gaps
// between choruses read DOWN then UP. Fully deterministic and cheap (no
// spectral clustering). Results are cached in the generic `analysis`
// table (AnalysisDao::TYPE_PHRASE) and republished onto the Track on
// every load, mirroring AnalyzerWaveform's store/reload flow.
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

    // Mean-square bass loudness per fixed-size hop (kHopSeconds).
    std::vector<double> m_hopEnergies;
    size_t m_hopSize;
    size_t m_hopFill;
    double m_hopAccum;
    // One-pole low-pass state/coefficient isolating the kick band.
    double m_lpState;
    double m_lpAlpha;
    mixxx::audio::SampleRate m_sampleRate;
    bool m_active;
};
