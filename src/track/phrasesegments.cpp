#include "track/phrasesegments.h"

#include <QDataStream>
#include <QIODevice>

namespace {
// Bump when the wire format changes; mismatching blobs are discarded.
constexpr quint32 kBlobMagic = 0x50485253; // "PHRS"
constexpr quint32 kBlobVersion = 1;
} // namespace

namespace mixxx {

QByteArray serializePhraseSegments(const PhraseSegments& segments) {
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_5_15);
    stream << kBlobMagic << kBlobVersion;
    stream << static_cast<quint32>(segments.size());
    for (const PhraseSegment& segment : segments) {
        stream << segment.startFrame << segment.endFrame
               << static_cast<qint32>(segment.type);
    }
    return data;
}

PhraseSegments deserializePhraseSegments(const QByteArray& data) {
    PhraseSegments segments;
    QDataStream stream(data);
    stream.setVersion(QDataStream::Qt_5_15);
    quint32 magic = 0;
    quint32 version = 0;
    stream >> magic >> version;
    if (magic != kBlobMagic || version != kBlobVersion) {
        return segments;
    }
    quint32 count = 0;
    stream >> count;
    segments.reserve(count);
    for (quint32 i = 0; i < count; ++i) {
        PhraseSegment segment;
        qint32 type = 0;
        stream >> segment.startFrame >> segment.endFrame >> type;
        if (stream.status() != QDataStream::Ok) {
            segments.clear();
            return segments;
        }
        segment.type = type;
        segments.append(segment);
    }
    return segments;
}

} // namespace mixxx
