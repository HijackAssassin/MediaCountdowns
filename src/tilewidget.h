#pragma once
#include <QWidget>
#include <QLabel>
#include <QTimer>
#include "outlinedlabel.h"
#include "countdownwidget.h"
#include "tiledata.h"

class TileWidget : public QWidget
{
    Q_OBJECT
public:
    explicit TileWidget(const TileData& data, QWidget* parent = nullptr);
    ~TileWidget() override;

    const TileData& tileData() const { return m_data; }
    void updateData(const TileData& data);
    void refreshImage();

    // Called by the shared static timer — only repaints if on visible tab
    void tick(bool tabVisible);

signals:
    void imageChanged(const QString& tileId, const QString& newImagePath);
    void tileDataChanged(const QString& tileId);
    void removeTile(const QString& tileId);
    void duplicateTile(const QString& tileId);
    void refetchRequested(const QString& tileId);
    void testNotification(const QString& tileId);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void buildUi();
    void applyImage(const QString& path);
    void recomputeTargetEpoch();   // pre-compute once; tick() just decrements
    void refreshCountdown();
    void refreshOverlays();
    void layoutOverlays();
    void openEditDialog();
    void deleteBackdropIfOwned(const QString& path);
    void fitOverlayFont(OutlinedLabel* label, const QString& text, int startPt);
    void invalidateFontCache();

    QString extractShowName()  const;
    QString formatEpisodeTag() const;
    QString formatTitleLine()  const;

    TileData         m_data;
    QWidget*         m_imageContainer  = nullptr;
    QLabel*          m_imageLabel      = nullptr;
    CountdownWidget* m_countdownWidget = nullptr;
    OutlinedLabel*   m_titleOverlay    = nullptr;
    int              m_cachedTitleFontPt = -1;
    QPixmap          m_cachedPixmap;

    // ── Pre-computed countdown ────────────────────────────────────────────────
    // Calculated once when data is set or edited; decremented by 1 each tick.
    // Avoids a full QDateTime diff every second for every tile.
    qint64  m_targetEpoch    = 0;   // target as secs-since-epoch
    qint64  m_remainingSecs  = 0;   // counts down; recomputed when data changes

    static constexpr int TITLE_PT = 18;

    // ── Shared static timer — ONE timer drives ALL tile instances ─────────────
    static QTimer*           s_sharedTimer;
    static QList<TileWidget*> s_allTiles;
    static void ensureSharedTimer();
};
