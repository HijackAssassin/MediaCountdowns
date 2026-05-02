#include "tilewidget.h"
#include "edittiledialog.h"
#include "outlinedlabel.h"
#include "countdownwidget.h"
#include "applogger.h"
#include <QVBoxLayout>
#include <QMouseEvent>
#include <QContextMenuEvent>
#include <QResizeEvent>
#include <QMenu>
#include <QPixmap>
#include <QDateTime>
#include <QFontMetrics>
#include <QFile>
#include <QApplication>

// ── Static member definitions ─────────────────────────────────────────────────
QTimer*            TileWidget::s_sharedTimer = nullptr;
QList<TileWidget*> TileWidget::s_allTiles;

// =============================================================================
TileWidget::TileWidget(const TileData& data, QWidget* parent)
    : QWidget(parent), m_data(data)
{
    s_allTiles.append(this);
    recomputeTargetEpoch();   // pre-compute once on construction
    buildUi();
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setMinimumWidth(160);
}

TileWidget::~TileWidget()
{
    s_allTiles.removeOne(this);
}

void TileWidget::buildUi()
{
    auto* vlay = new QVBoxLayout(this);
    vlay->setContentsMargins(0,0,0,0);
    vlay->setSpacing(0);

    m_imageContainer = new QWidget(this);
    m_imageContainer->setStyleSheet("background:#000;");

    m_imageLabel = new QLabel(m_imageContainer);
    m_imageLabel->setAlignment(Qt::AlignCenter);
    m_imageLabel->setStyleSheet("background:#000;");
    applyImage(m_data.imagePath);

    m_countdownWidget = new CountdownWidget(m_imageContainer);
    m_titleOverlay    = new OutlinedLabel(m_imageContainer);
    m_titleOverlay->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_titleOverlay->setWordWrap(false);
    m_titleOverlay->setAttribute(Qt::WA_TranslucentBackground);
    m_titleOverlay->setAutoFillBackground(false);

    vlay->addWidget(m_imageContainer);
    refreshOverlays();
}

// =============================================================================
//  recomputeTargetEpoch — called once when tile data is set or changed.
//  Stores the target as seconds-since-epoch so tick() can simply decrement
//  m_remainingSecs by 1 each second instead of doing a full datetime diff.
// =============================================================================
void TileWidget::recomputeTargetEpoch()
{
    if (!m_data.hasDate()) {
        m_targetEpoch   = 0;
        m_remainingSecs = 0;
        return;
    }
    QTime t = m_data.effectiveTime().isValid() ? m_data.effectiveTime() : QTime(0, 0, 0);
    QDateTime target(m_data.effectiveDate(), t, Qt::LocalTime);
    m_targetEpoch   = target.toSecsSinceEpoch();
    // Compute remaining once accurately using the actual current time
    m_remainingSecs = qMax(qint64(0),
        m_targetEpoch - QDateTime::currentDateTimeUtc().toSecsSinceEpoch()
            + QDateTime::currentDateTime().offsetFromUtc());
    // Simpler: just use secsTo on the local datetime
    m_remainingSecs = qMax(qint64(0), QDateTime::currentDateTime().secsTo(target));
}

// =============================================================================
//  tick() — called by MainWindow::onGlobalTick() once per second.
//  Recomputes remaining seconds from the real wall clock every tick.
//  The Qt 1000ms timer drifts — decrementing causes displayed seconds to
//  fall behind real time. One secsTo() call per tile per second is negligible.
// =============================================================================
void TileWidget::tick(bool tabVisible)
{
    if (!m_data.hasDate() || m_data.isExpired()) return;
    if (!tabVisible) return;

    m_remainingSecs = qMax(qint64(0),
        QDateTime::currentDateTime().secsTo(
            QDateTime(m_data.effectiveDate(),
                      m_data.effectiveTime().isValid()
                          ? m_data.effectiveTime() : QTime(0, 0, 0))));

    m_countdownWidget->setSeconds(m_remainingSecs);
}

void TileWidget::refreshCountdown()
{
    if (!m_data.hasDate()) {
        m_countdownWidget->setNoDate(); return;
    }
    if (m_data.isExpired()) {
        m_countdownWidget->setExpired(); return;
    }
    // Re-sync from real time (called on data change / full refresh, not every tick)
    recomputeTargetEpoch();
    m_countdownWidget->setSeconds(m_remainingSecs);
}

void TileWidget::refreshOverlays()
{
    // Full refresh: update countdown + rebuild title + recalc font + layout
    // Only call this when data actually changes, not on every tick.
    refreshCountdown();
    QString titleLine = formatTitleLine();
    m_titleOverlay->setText(titleLine);
    m_cachedTitleFontPt = -1;   // force recalc since title content changed
    fitOverlayFont(m_titleOverlay, titleLine, TITLE_PT);
    layoutOverlays();
}

// =============================================================================
void TileWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (!m_imageContainer) return;
    int w = event->size().width();
    int imgH = w * 9 / 16;
    m_imageContainer->setFixedHeight(imgH);
    m_imageLabel->setGeometry(0, 0, w, imgH);
    if (!m_cachedPixmap.isNull()) {
        m_imageLabel->setPixmap(
            m_cachedPixmap.scaled(w, imgH, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
    }
    // Width changed — invalidate cached font size so it recalculates
    m_cachedTitleFontPt = -1;
    fitOverlayFont(m_titleOverlay, m_titleOverlay->text(), TITLE_PT);
    layoutOverlays();
}

void TileWidget::layoutOverlays()
{
    if (!m_imageContainer) return;
    int w    = m_imageContainer->width();
    int imgH = m_imageContainer->height();
    if (w <= 0 || imgH <= 0) return;
    int tH = qMax(m_titleOverlay->sizeHint().height(), imgH / 6);
    m_countdownWidget->setGeometry(0, 0, w, imgH);
    m_countdownWidget->scaleFonts(w);
    m_titleOverlay->setGeometry(0, imgH - tH, w, tH);
    m_titleOverlay->raise();
}

void TileWidget::fitOverlayFont(OutlinedLabel* label, const QString& text, int startPt)
{
    if (!label || !m_imageContainer) return;

    // Use cached size if available (avoid expensive recalc every second)
    int sz = m_cachedTitleFontPt;

    if (sz < 0) {
        // First calculation — bold, so measure with bold
        int availW = m_imageContainer->width() - 20;
        sz = startPt;
        if (availW > 0) {
            QFont f = label->font(); f.setBold(true); f.setPointSize(sz);
            while (sz > 7 && QFontMetrics(f).horizontalAdvance(text) > availW) {
                --sz; f.setPointSize(sz);
            }
        }
        m_cachedTitleFontPt = sz;
    }

    // Only call setFont when the size actually changed
    QFont f = label->font();
    if (f.pointSize() != sz || !f.bold()) {
        f.setBold(true);
        f.setPointSize(sz);
        label->setFont(f);
        QPalette pal = label->palette();
        pal.setColor(QPalette::WindowText, Qt::white);
        label->setPalette(pal);
    }
}

void TileWidget::invalidateFontCache()
{
    m_cachedTitleFontPt = -1;
}

// =============================================================================
QString TileWidget::extractShowName() const
{
    QString t = m_data.displayTitle();
    int bullet = t.indexOf(" \xe2\x80\xa2 ");
    return (bullet >= 0) ? t.left(bullet) : t;
}

// =============================================================================
//  formatEpisodeTag — returns the status label in compact S##E## notation.
//
//  The scraper already stores statusLabel as "S02E01" or "S02E01+E02".
//  We re-parse and zero-pad for robustness (in case of legacy data).
//
//  Examples:
//    "S2E1"       → "S02E01"
//    "S4E1+E2"    → "S04E01+E02"
//    "Last Episode"→ ""  (non-episode labels are filtered upstream)
// =============================================================================
QString TileWidget::formatEpisodeTag() const
{
    QString sl = m_data.statusLabel;
    if (sl.isEmpty() || !sl.startsWith('S') || !sl.contains('E')) return {};

    int eIdx   = sl.indexOf('E');
    int season = sl.mid(1, eIdx - 1).toInt();
    QString epPart = sl.mid(eIdx + 1);  // everything after the first 'E'

    if (epPart.contains('+')) {
        // Multi-episode e.g. "S02E01+E02"
        QStringList parts = epPart.split('+');
        QString result = QString("S%1").arg(season, 2, 10, QChar('0'));
        for (int i = 0; i < parts.size(); ++i) {
            QString e = parts[i];
            if (e.startsWith('E')) e = e.mid(1);   // strip leading 'E' if present
            if (i == 0) result += QString("E%1").arg(e.toInt(), 2, 10, QChar('0'));
            else        result += QString("+E%1").arg(e.toInt(), 2, 10, QChar('0'));
        }
        return result;
    }

    // Single episode e.g. "S02E01"
    return QString("S%1E%2")
        .arg(season,        2, 10, QChar('0'))
        .arg(epPart.toInt(), 2, 10, QChar('0'));
}

QString TileWidget::formatTitleLine() const
{
    QString name = extractShowName();
    if (!m_data.hasDate())
        return name + "  \xe2\x80\xa2  No Release Date";
    if (m_data.isExpired())
        return name + "  \xe2\x80\xa2  " + m_data.displayDate();
    QString epTag = (m_data.mediaType == "tv") ? formatEpisodeTag() : QString();
    QString line = name;
    if (!epTag.isEmpty()) line += " " + epTag;
    line += "  \xe2\x80\xa2  " + m_data.displayDate();
    return line;
}

// =============================================================================
void TileWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) openEditDialog();
    QWidget::mousePressEvent(event);
}

void TileWidget::contextMenuEvent(QContextMenuEvent* event)
{
    QMenu menu(this);
    auto* editAct   = menu.addAction("Edit Tile");
    auto* dupAct    = menu.addAction("Duplicate Tile");
    menu.addSeparator();
    auto* removeAct = menu.addAction("Remove Tile");
    menu.addSeparator();
    auto* testAct   = menu.addAction("Test Notification");
    QAction* chosen = menu.exec(event->globalPos());

    if (chosen == editAct) {
        openEditDialog();
    } else if (chosen == dupAct) {
        QString id = m_data.id;
        QMetaObject::invokeMethod(this, [this, id]{ emit duplicateTile(id); },
                                  Qt::QueuedConnection);
    } else if (chosen == removeAct) {
        QString id = m_data.id;
        QMetaObject::invokeMethod(this, [this, id]{ emit removeTile(id); },
                                  Qt::QueuedConnection);
    } else if (chosen == testAct) {
        emit testNotification(m_data.id);
    }
}

void TileWidget::openEditDialog()
{
    EditTileDialog dlg(m_data, this);

    // Wire the "Test Notification" button inside the dialog to our signal.
    // Since dlg.exec() runs its own event loop, signals from the dialog ARE
    // processed — this connection fires while the dialog is still open.
    connect(&dlg, &EditTileDialog::testNotificationRequested,
            this, [this]{ emit testNotification(m_data.id); });

    if (dlg.exec() != QDialog::Accepted) return;

    if (dlg.wantsRemove()) {
        // IMPORTANT: Never emit removeTile synchronously here.
        // We're inside mousePressEvent → openEditDialog. A direct emit would
        // call onRemoveTile → delete this, then execution returns to this
        // method and then to mousePressEvent on a destroyed object → crash.
        // Queueing defers the deletion until after the current event fully
        // unwinds, which is safe.
        QString id = m_data.id;
        QMetaObject::invokeMethod(this, [this, id]{ emit removeTile(id); },
                                  Qt::QueuedConnection);
        return;
    }

    bool changed = false;
    QString newTitle      = dlg.customTitle();
    QString effectiveCustom = (newTitle == m_data.title) ? QString() : newTitle;
    if (effectiveCustom != m_data.customTitle) { m_data.customTitle = effectiveCustom; changed = true; }

    QTime newTime = dlg.customAirTime();
    if (newTime != m_data.customAirTime) { m_data.customAirTime = newTime; changed = true; }

    QDate newDate = dlg.customDate();
    bool wantsDate = dlg.dateChecked();  // reliable — tracks actual user intent
    bool hadDate   = m_data.hasDate() || m_data.noDateOverride;

    bool dateChanged = (!wantsDate && hadDate)            // user removed date
                    || (wantsDate && m_data.noDateOverride)  // user re-enabled date
                    || (wantsDate && newDate != m_data.customDate);  // date value changed

    if (dateChanged) {
        if (!wantsDate) {
            m_data.noDateOverride = true;
            m_data.customDate     = QDate();
            m_data.customDateStr  = "";
        } else {
            m_data.noDateOverride = false;
            m_data.customDate     = newDate;
            m_data.customDateStr  = dlg.customDateStr();
        }
        changed = true;
    }

    // Loop fields
    if (m_data.isLooped      != dlg.isLooped())       { m_data.isLooped      = dlg.isLooped();       changed = true; }
    if (m_data.loopInterval  != dlg.loopInterval())   { m_data.loopInterval  = dlg.loopInterval();   changed = true; }
    if (m_data.loopWeekday   != dlg.loopWeekday())    { m_data.loopWeekday   = dlg.loopWeekday();    changed = true; }
    if (m_data.loopDayOfMonth!= dlg.loopDayOfMonth()) { m_data.loopDayOfMonth= dlg.loopDayOfMonth(); changed = true; }
    if (m_data.presetType    != dlg.presetType())     { m_data.presetType    = dlg.presetType();     changed = true; }

    QString newPath = dlg.selectedImagePath();
    if (dlg.imageWasReset() || newPath != m_data.imagePath) {
        deleteBackdropIfOwned(m_data.imagePath);
        m_data.imagePath = newPath;
        applyImage(newPath);
        changed = true;
    }

    if (changed) {
        refreshOverlays();
        emit tileDataChanged(m_data.id);
    }
    if (dlg.anyResetPressed()) emit refetchRequested(m_data.id);
}

// =============================================================================
void TileWidget::applyImage(const QString& path)
{
    if (!m_imageLabel) return;
    if (path.isEmpty()) {
        m_cachedPixmap = QPixmap();
        m_imageLabel->clear(); m_imageLabel->setStyleSheet("background:#000;"); return;
    }
    QPixmap px(path);
    if (px.isNull()) { m_cachedPixmap = QPixmap(); m_imageLabel->clear(); return; }
    m_cachedPixmap = px;   // cache the full-res pixmap — resize uses this, no more disk hits
    int w = m_imageLabel->width()  > 0 ? m_imageLabel->width()  : 400;
    int h = m_imageLabel->height() > 0 ? m_imageLabel->height() : 225;
    m_imageLabel->setPixmap(
        m_cachedPixmap.scaled(w, h, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
    m_imageLabel->setStyleSheet("background:#000;");
}

void TileWidget::deleteBackdropIfOwned(const QString& path)
{
    if (path.isEmpty()) return;
    if (path.contains("/fetched_images/") || path.contains("\\fetched_images\\") ||
        path.contains("/custom_images/")  || path.contains("\\custom_images\\"))
        QFile::remove(path);
}

void TileWidget::refreshImage() { applyImage(m_data.imagePath); }

void TileWidget::updateData(const TileData& data)
{
    m_data = data;
    recomputeTargetEpoch();   // date/time may have changed — re-compute once
    applyImage(m_data.imagePath);
    refreshOverlays();
}
