#include "widget/wphrasebar.h"

#include <QPainter>
#include <QPaintEvent>

#include "moc_wphrasebar.cpp"
#include "track/track.h"

namespace {

// Background when no track / no analysis yet - same #202329 stripe the
// decorative placeholder used, so the bar never reads as a rendering bug.
const QColor kBackgroundColor(0x20, 0x23, 0x29);

// Semantic palette indexed by AnalyzerPhrase's SectionRole (0=INTRO,
// 1=UP, 2=CHORUS, 3=DOWN, 4=OUTRO), matching the Rekordbox scheme. Hues
// follow the Rekordbox desktop bar but pushed brighter/more saturated:
// the exact sampled values read washed-out ("bordeaux", "vert malade")
// on the Pi's low-gamut 1024x600 panel, and the purple needs a magenta
// lean to not blend with the royal-blue waveform lows (#1f4dd8).
const QColor kSegmentPalette[] = {
        QColor(0xe8, 0x35, 0x28), // 0 INTRO - vivid red
        QColor(0xb0, 0x4c, 0xf2), // 1 UP - magenta-leaning purple
        QColor(0x37, 0xd0, 0x43), // 2 CHORUS - vivid green
        QColor(0xb0, 0x7d, 0x2e), // 3 DOWN - warm brown
        QColor(0x66, 0xa8, 0xe8), // 4 OUTRO - sky blue
        QColor(0xe3, 0xc9, 0x3c), // 5 fallback - yellow (unused normally)
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
