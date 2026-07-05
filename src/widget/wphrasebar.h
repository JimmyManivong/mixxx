#pragma once

#include <QColor>
#include <QDomNode>
#include <QWidget>

#include "skin/legacy/skincontext.h"
#include "track/phrasesegments.h"
#include "track/track_decl.h"
#include "widget/wwidget.h"

// CUSTOM: Rekordbox/XDJ-AZ style phrase bar for the RekordboxPi skin's
// preview cards. Renders the track's musical structure sections (from
// AnalyzerPhrase) as colored blocks proportional to their duration -
// sections of the same similarity cluster share a color, like the
// hardware's phrase bar minus the semantic labels.
class WPhraseBar : public WWidget {
    Q_OBJECT
  public:
    explicit WPhraseBar(QWidget* pParent);

    void setup(const QDomNode& node, const SkinContext& context);

  public slots:
    void slotTrackLoaded(TrackPointer pTrack);
    void slotLoadingTrack(TrackPointer pNewTrack, TrackPointer pOldTrack);

  private slots:
    void slotPhraseSegmentsUpdated();

  protected:
    void paintEvent(QPaintEvent* pEvent) override;

  private:
    TrackPointer m_pCurrentTrack;
    mixxx::PhraseSegments m_segments;
};
