#pragma once

#include <QColor>
#include <QString>
#include <QVector>

#include "track/track_decl.h"
#include "widget/wwidget.h"

class QDomNode;
class QProcess;
class SkinContext;

/// CUSTOM (Rekordbox-style phrase bar): a thin coloured strip, drawn BELOW the
/// track overview, that segments the song into musical phrases
/// (INTRO / UP / CHORUS / DOWN / OUTRO) with a text label per section.
///
/// The segmentation is produced offline by tools/phrase_analysis.py (librosa
/// Music-Structure-Analysis) and cached as one JSON file per track, keyed by
/// sha1(absolute path). When a track is loaded:
///   * if the cache exists -> load + draw immediately;
///   * otherwise -> launch the analyzer in the background (QProcess) and draw
///     as soon as it finishes (option "auto on load").
class WPhraseBar : public WWidget {
    Q_OBJECT
  public:
    WPhraseBar(QWidget* pParent, const QString& group);

    void setup(const QDomNode& node, const SkinContext& context);

  public slots:
    void slotTrackLoaded(TrackPointer pTrack);
    void slotLoadingTrack(TrackPointer pNewTrack, TrackPointer pOldTrack);

  protected:
    void paintEvent(QPaintEvent* e) override;

  private slots:
    void slotAnalysisFinished(int exitCode);

  private:
    struct Segment {
        double start;
        double end;
        QString label;
    };

    void loadForLocation(const QString& location);
    bool loadCache(const QString& location);
    void startAnalysis(const QString& location);
    QString cachePathFor(const QString& location) const;
    QColor colorForLabel(const QString& label) const;

    QString m_group;
    QVector<Segment> m_segments;
    double m_duration;

    // Tooling / cache locations (overridable from the skin node).
    QString m_python;
    QString m_script;
    QString m_cacheDir;

    QString m_analyzingLocation;
    QProcess* m_pProc;

    QColor m_textColor;
    QColor m_borderColor;
};
