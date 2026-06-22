#include "widget/wphrasebar.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDomNode>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QProcess>
#include <QStandardPaths>

#include "skin/legacy/skincontext.h"
#include "track/track.h"
#include "moc_wphrasebar.cpp"

namespace {
// Defaults match tools/phrase_analysis.py. Overridable from the skin node.
const QString kDefaultPython = QStringLiteral("/opt/anaconda3/bin/python3");
const QString kDefaultScript =
        QStringLiteral("/Users/jimmymanivong/Projects/mixxx/tools/phrase_analysis.py");

QString defaultCacheDir() {
    return QDir::homePath() +
            QStringLiteral("/Library/Containers/org.mixxx.mixxx/Data/Library/"
                           "Application Support/Mixxx/phrase_cache");
}
} // namespace

WPhraseBar::WPhraseBar(QWidget* pParent, const QString& group)
        : WWidget(pParent),
          m_group(group),
          m_duration(0.0),
          m_python(kDefaultPython),
          m_script(kDefaultScript),
          m_cacheDir(defaultCacheDir()),
          m_pProc(nullptr),
          m_textColor(QColor(0xff, 0xff, 0xff)),
          m_borderColor(QColor(0x10, 0x10, 0x10)) {
    setFocusPolicy(Qt::NoFocus);
}

void WPhraseBar::setup(const QDomNode& node, const SkinContext& context) {
    QString s;
    if (context.hasNodeSelectString(node, "Python", &s) && !s.isEmpty()) {
        m_python = s;
    }
    if (context.hasNodeSelectString(node, "Script", &s) && !s.isEmpty()) {
        m_script = s;
    }
    if (context.hasNodeSelectString(node, "CacheDir", &s) && !s.isEmpty()) {
        m_cacheDir = s;
    }
    if (context.hasNodeSelectString(node, "TextColor", &s)) {
        m_textColor = QColor(s);
    }
    if (context.hasNodeSelectString(node, "BorderColor", &s)) {
        m_borderColor = QColor(s);
    }
}

QString WPhraseBar::cachePathFor(const QString& location) const {
    const QString key = QString::fromLatin1(
            QCryptographicHash::hash(location.toUtf8(), QCryptographicHash::Sha1)
                    .toHex());
    return m_cacheDir + QStringLiteral("/") + key + QStringLiteral(".json");
}

QColor WPhraseBar::colorForLabel(const QString& label) const {
    // Strip a trailing number ("CHORUS 2" -> "CHORUS").
    QString base = label;
    const int sp = base.indexOf(QLatin1Char(' '));
    if (sp > 0) {
        base = base.left(sp);
    }
    base = base.toUpper();
    if (base == QStringLiteral("INTRO") || base == QStringLiteral("OUTRO")) {
        return QColor(0x6b, 0x72, 0x80); // grey
    }
    if (base == QStringLiteral("CHORUS")) {
        return QColor(0xe5, 0x3e, 0x6a); // pink/red (the drop)
    }
    if (base == QStringLiteral("UP")) {
        return QColor(0xf0, 0x9b, 0x2e); // orange (build-up)
    }
    if (base == QStringLiteral("DOWN")) {
        return QColor(0x2f, 0x8a, 0xd8); // blue (breakdown)
    }
    if (base == QStringLiteral("VERSE")) {
        return QColor(0x3f, 0xb0, 0x6b); // green
    }
    return QColor(0x55, 0x5b, 0x66);
}

void WPhraseBar::slotLoadingTrack(TrackPointer pNewTrack, TrackPointer pOldTrack) {
    Q_UNUSED(pNewTrack);
    Q_UNUSED(pOldTrack);
    // Clear while the new track is loading so we never show stale phrases.
    m_segments.clear();
    m_duration = 0.0;
    update();
}

void WPhraseBar::slotTrackLoaded(TrackPointer pTrack) {
    if (!pTrack) {
        m_segments.clear();
        m_duration = 0.0;
        update();
        return;
    }
    loadForLocation(pTrack->getLocation());
}

void WPhraseBar::loadForLocation(const QString& location) {
    if (location.isEmpty()) {
        return;
    }
    if (loadCache(location)) {
        update();
        return;
    }
    // No cache yet -> analyze in the background (auto on load).
    startAnalysis(location);
}

bool WPhraseBar::loadCache(const QString& location) {
    QFile f(cachePathFor(location));
    if (!f.exists() || !f.open(QIODevice::ReadOnly)) {
        return false;
    }
    const QByteArray data = f.readAll();
    f.close();
    const QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) {
        return false;
    }
    const QJsonObject obj = doc.object();
    m_duration = obj.value(QStringLiteral("duration")).toDouble(0.0);
    m_segments.clear();
    const QJsonArray segs = obj.value(QStringLiteral("segments")).toArray();
    for (const QJsonValue& v : segs) {
        const QJsonObject so = v.toObject();
        Segment seg;
        seg.start = so.value(QStringLiteral("start")).toDouble(0.0);
        seg.end = so.value(QStringLiteral("end")).toDouble(0.0);
        seg.label = so.value(QStringLiteral("label")).toString();
        m_segments.append(seg);
    }
    if (m_duration <= 0.0 && !m_segments.isEmpty()) {
        m_duration = m_segments.last().end;
    }
    return !m_segments.isEmpty();
}

void WPhraseBar::startAnalysis(const QString& location) {
    if (m_script.isEmpty() || !QFileInfo::exists(m_script)) {
        return;
    }
    // One analysis at a time; if a different track comes in, restart.
    if (m_pProc && m_pProc->state() != QProcess::NotRunning) {
        if (m_analyzingLocation == location) {
            return; // already analyzing this track
        }
        m_pProc->kill();
        m_pProc->waitForFinished(100);
    }
    if (!m_pProc) {
        m_pProc = new QProcess(this);
        connect(m_pProc,
                QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this,
                [this](int code, QProcess::ExitStatus) { slotAnalysisFinished(code); });
    }
    m_analyzingLocation = location;
    QDir().mkpath(m_cacheDir);
    m_pProc->start(m_python,
            {m_script,
                    location,
                    QStringLiteral("--cache-dir"),
                    m_cacheDir});
}

void WPhraseBar::slotAnalysisFinished(int exitCode) {
    if (exitCode != 0 || m_analyzingLocation.isEmpty()) {
        return;
    }
    if (loadCache(m_analyzingLocation)) {
        update();
    }
}

void WPhraseBar::paintEvent(QPaintEvent* /*e*/) {
    if (m_segments.isEmpty() || m_duration <= 0.0) {
        return;
    }
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);

    const double w = width();
    const double h = height();
    const double pxPerSec = w / m_duration;

    QFont font = p.font();
    font.setPixelSize(qMax(8, static_cast<int>(h * 0.55)));
    font.setBold(true);
    p.setFont(font);

    for (const Segment& seg : m_segments) {
        double x0 = seg.start * pxPerSec;
        double x1 = seg.end * pxPerSec;
        if (x1 <= x0) {
            continue;
        }
        QRectF rect(x0, 0.0, x1 - x0, h);
        const QColor c = colorForLabel(seg.label);
        p.fillRect(rect, c);
        // thin separators
        p.setPen(QPen(m_borderColor, 1.0));
        p.drawLine(QPointF(x0, 0.0), QPointF(x0, h));

        // Label, clipped to the segment. Hide it if the box is too narrow.
        if (rect.width() > 22.0) {
            p.setPen(m_textColor);
            const QRectF textRect = rect.adjusted(4.0, 0.0, -2.0, 0.0);
            const QString txt = p.fontMetrics().elidedText(
                    seg.label, Qt::ElideRight, static_cast<int>(textRect.width()));
            p.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, txt);
        }
    }
    // outer border
    p.setPen(QPen(m_borderColor, 1.0));
    p.setBrush(Qt::NoBrush);
    p.drawRect(QRectF(0.5, 0.5, w - 1.0, h - 1.0));
}
