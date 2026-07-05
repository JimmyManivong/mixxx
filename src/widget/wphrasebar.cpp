#include "widget/wphrasebar.h"

#include <QPainter>
#include <QPaintEvent>

#include "moc_wphrasebar.cpp"
#include "track/track.h"

namespace {

// Background when no track / no analysis yet - same #202329 stripe the
// decorative placeholder used, so the bar never reads as a rendering bug.
const QColor kBackgroundColor(0x20, 0x23, 0x29);

// Section palette, sampled from the XDJ-AZ reference photo's phrase bar
// (red / purple / tan / green / blue-grey / sand). Types are cluster ids,
// so consecutive different sections get visibly different colors.
const QColor kSegmentPalette[] = {
        QColor(0x94, 0x4e, 0x44), // red
        QColor(0x93, 0x64, 0xc6), // purple
        QColor(0x92, 0xa3, 0x74), // green
        QColor(0xae, 0x92, 0x7c), // tan
        QColor(0x9a, 0x99, 0xb0), // blue-grey
        QColor(0xc6, 0xba, 0x64), // sand
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
    for (const mixxx::PhraseSegment& segment : m_segments) {
        const int x0 = static_cast<int>(segment.startFrame / totalFrames * w);
        const int x1 = static_cast<int>(segment.endFrame / totalFrames * w);
        const QColor& color =
                kSegmentPalette[segment.type % kSegmentPaletteSize];
        painter.fillRect(x0, 0, qMax(1, x1 - x0), h, color);
    }
}
