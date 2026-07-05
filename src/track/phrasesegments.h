#pragma once

#include <QByteArray>
#include <QVector>

namespace mixxx {

// CUSTOM: one section of a track's musical structure (RekordboxPi phrase
// bar), produced by AnalyzerPhrase via qm-dsp's ClusterMeltSegmenter.
// Sections sharing the same `type` are musically similar passages (same
// cluster), so the UI can color them identically - the same idea as
// Rekordbox's phrase bar, minus the semantic intro/chorus labels.
struct PhraseSegment {
    double startFrame; // first audio frame of the section
    double endFrame;   // one past the last audio frame of the section
    int type;          // cluster id in [0, nclusters)
};

typedef QVector<PhraseSegment> PhraseSegments;

// (De)serialization for the `analysis` table blob (AnalysisDao TYPE_PHRASE).
QByteArray serializePhraseSegments(const PhraseSegments& segments);
PhraseSegments deserializePhraseSegments(const QByteArray& data);

} // namespace mixxx
