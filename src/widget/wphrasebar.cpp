#include "widget/wphrasebar.h"

#include <QPainter>
#include <QPaintEvent>

#include "moc_wphrasebar.cpp"
#include "track/track.h"

namespace {

// Background when no track / no analysis yet - same #202329 stripe the
// decorative placeholder used, so the bar never reads as a rendering bug.
const QColor kBackgroundColor(0x20, 0x23, 0x29);

// Section palette: exact colors pixel-sampled from the Rekordbox desktop
// phrase bar (screenshot 2026-07-06): INTRO red, UP purple, CHORUS green,
// DOWN olive, UP2 blue-violet, OUTRO steel blue. Types are cluster ids, so
// the mapping to labels is arbitrary but the palette matches 1:1.
const QColor kSegmentPalette[] = {
        QColor(0xb7, 0x26, 0x19), // Rekordbox INTRO red
        QColor(0x81, 0x38, 0xf6), // Rekordbox UP purple
        QColor(0x4e, 0xa7, 0x30), // Rekordbox CHORUS green
        QColor(0x95, 0x75, 0x3a), // Rekordbox DOWN olive
        QColor(0x62, 0x35, 0xf5), // Rekordbox UP2 blue-violet
        QColor(0x5d, 0x86, 0xbe), // Rekordbox OUTRO steel blue
};
constexpr int kSegmentPaletteSize =
        sizeof(kSegmentPalette) / sizeof(kSegmentPalette[0]);

} // namespace

WPhraseBar::WPhraseBar(QWidget* pParent)
        : WWidget(pParent) {
}

void WPhraseBar::setup(const QDomNode& node, const SkinContext& context) {
    Q_UNUSED(node);
    Q_UNUSED(context);
}

void WPhraseBar::slotTrackLoaded(TrackPointer pTrack) {
    if (m_pCurrentTrack) {
        disconnect(m_pCurrentTrack.get(), nullptr, this, nullptr);
    }
    m_pCurrentTrack = pTrack;
    if (pTrack) {
        connect(pTrack.get(),
                &Track::phraseSegmentsUpdated,
                this,
                &WPhraseBar::slotPhraseSegmentsUpdated);
        m_segments = pTrack->getPhraseSegments();
    } else {
        m_segments.clear();
    }
    update();
}

void WPhraseBar::slotLoadingTrack(TrackPointer pNewTrack, TrackPointer pOldTrack) {
    Q_UNUSED(pNewTrack);
    Q_UNUSED(pOldTrack);
    if (m_pCurrentTrack) {
        disconnect(m_pCurrentTrack.get(), nullptr, this, nullptr);
    }
    m_pCurrentTrack.reset();
    m_segments.clear();
    update();
}

void WPhraseBar::slotPhraseSegmentsUpdated() {
    if (m_pCurrentTrack) {
        m_segments = m_pCurrentTrack->getPhraseSegments();
    }
    update();
}

void WPhraseBar::paintEvent(QPaintEvent* pEvent) {
    Q_UNUSED(pEvent);
    QPainter painter(this);
    painter.fillRect(rect(), kBackgroundColor);
    if (m_segments.isEmpty()) {
        return;
    }
    const double totalFrames = m_segments.last().endFrame;
    if (totalFrames <= 0) {
        return;
    }
    const int w = width();
    const int h = height();
    bool isFirst = true;
    for (const mixxx::PhraseSegment& segment : m_segments) {
        const int x0 = static_cast<int>(segment.startFrame / totalFrames * w);
        const int x1 = static_cast<int>(segment.endFrame / totalFrames * w);
        const QColor& color =
                kSegmentPalette[segment.type % kSegmentPaletteSize];
        painter.fillRect(x0, 0, qMax(1, x1 - x0), h, color);
        // Dark seam between sections so equal-luminance neighbors still
        // read as separate phrases.
        if (!isFirst) {
            painter.fillRect(x0, 0, 1, h, kBackgroundColor);
        }
        isFirst = false;
    }
}
