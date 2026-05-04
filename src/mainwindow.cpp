#include "mainwindow.h"
#include "tilewidget.h"
#include "jsonmanager.h"
#include "customtiledialog.h"
#include "applogger.h"
#include <QUuid>

#include <QDesktopServices>
#include <QSettings>
#include <QUrl>
#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLocalSocket>
#include <QFrame>
#include <QFile>
#include <QDir>
#include <QSet>
#include <QStandardPaths>
#include <QDebug>
#include <QResizeEvent>
#include <QWheelEvent>
#include <QEvent>
#include <QFileInfo>
#include <QApplication>
#include <QStyle>
#include <QVariantAnimation>
#include <QEasingCurve>
#include <QScrollBar>
#include <QDialog>
#include <QPlainTextEdit>
#include <QShortcut>
#include <QSystemTrayIcon>
#include <QFileDialog>
#include <QMessageBox>
#include <QProcess>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDirIterator>
#include <algorithm>

static bool tileOrder(const TileData& a, const TileData& b)
{
    QDate da = a.effectiveDate(), db = b.effectiveDate();
    if (!da.isValid() && !db.isValid()) return false;
    if (!da.isValid()) return false;   // no-date tiles sort to end
    if (!db.isValid()) return true;
    if (da != db) return da < db;      // different dates: earlier first
    // Same date — break tie by time. Invalid time = midnight (00:00).
    QTime ta = a.effectiveTime().isValid() ? a.effectiveTime() : QTime(0, 0);
    QTime tb = b.effectiveTime().isValid() ? b.effectiveTime() : QTime(0, 0);
    return ta < tb;
}

int MainWindow::tabForTile(const TileData& td) const
{
    if (!td.hasDate())  return TAB_OTHER;
    if (td.isExpired()) return TAB_RELEASED;
    return TAB_ACTIVE;
}

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_scraper(new TmdbScraper(this))
{
    setWindowTitle("Media Countdowns");
    setMinimumSize(700, 500);

    connect(m_scraper, &TmdbScraper::searchResultsReady, this, &MainWindow::onSearchResultsReady);
    connect(m_scraper, &TmdbScraper::creditsReady,       this, &MainWindow::onCreditsReady);
    connect(m_scraper, &TmdbScraper::dataReady,          this, &MainWindow::onScraperDataReady);
    connect(m_scraper, &TmdbScraper::tileRefreshed,      this, &MainWindow::onTileRefreshed);
    connect(m_scraper, &TmdbScraper::posterReady,        this, &MainWindow::onPosterReady);
    connect(m_scraper, &TmdbScraper::scraperError,       this, &MainWindow::onScraperError);

    auto* central = new QWidget(this);
    central->setStyleSheet("background-color:#1a1a1a;");
    setCentralWidget(central);

    auto* mainLayout = new QVBoxLayout(central);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    m_tabs = new QTabWidget(central);
    m_tabs->setStyleSheet(R"(
        QTabWidget::pane { border: none; background:#1a1a1a; }
        QTabBar::tab {
            background:#111111; color:#888888;
            padding:8px 24px; font-size:13px;
            border-bottom:2px solid transparent;
        }
        QTabBar::tab:selected  { color:#ffffff; border-bottom:2px solid #0078d4; }
        QTabBar::tab:hover     { color:#cccccc; }
    )");

    const QStringList tabNames = {"Countdowns", "Released", "Other"};
    for (int t = 0; t < 3; ++t) {
        auto* scroll = new QScrollArea;
        m_scrollAreas[t] = scroll;
        scroll->setWidgetResizable(true);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setStyleSheet(
            "QScrollArea { background:#1a1a1a; border:none; }"
            "QScrollBar:vertical { background:#1e1e1e; width:8px; }"
            "QScrollBar::handle:vertical { background:#444; border-radius:4px; }"
            "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0; }");

        m_tabContainers[t] = new QWidget;
        m_tabContainers[t]->setStyleSheet("background:#1a1a1a;");

        m_grids[t] = new QGridLayout(m_tabContainers[t]);
        m_grids[t]->setContentsMargins(0, 0, 0, 0);
        m_grids[t]->setSpacing(1);
        for (int c = 0; c < COLS; ++c)
            m_grids[t]->setColumnStretch(c, 1);
        m_tabContainers[t]->setLayout(m_grids[t]);

        scroll->setWidget(m_tabContainers[t]);
        m_tabs->addTab(scroll, tabNames[t]);

        m_scrollTarget[t] = 0;
        m_scrollAnim[t] = new QVariantAnimation(this);
        m_scrollAnim[t]->setDuration(350);
        m_scrollAnim[t]->setEasingCurve(QEasingCurve::OutCubic);
        const int ti = t;
        connect(m_scrollAnim[t], &QVariantAnimation::valueChanged,
                this, [this, ti](const QVariant& val) {
            m_scrollAreas[ti]->verticalScrollBar()->setValue(val.toInt());
        });
        installSmoothScroll(scroll);
    }
    mainLayout->addWidget(m_tabs, 1);

    // ── Import / Export corner buttons ────────────────────────────────────
    auto* cornerWidget = new QWidget(m_tabs);
    auto* cornerLayout = new QHBoxLayout(cornerWidget);
    cornerLayout->setContentsMargins(0, 2, 8, 0);
    cornerLayout->setSpacing(6);

    auto* exportBtn = new QPushButton("Export", cornerWidget);
    exportBtn->setFixedHeight(28);
    exportBtn->setStyleSheet(
        "QPushButton { background:#1e3a1e; color:#66cc66; border:1px solid #2a5a2a; "
        "border-radius:5px; font-size:12px; padding:0 14px; }"
        "QPushButton:hover { background:#2a4a2a; }");
    connect(exportBtn, &QPushButton::clicked, this, &MainWindow::onExportClicked);

    auto* importBtn = new QPushButton("Import", cornerWidget);
    importBtn->setFixedHeight(28);
    importBtn->setStyleSheet(
        "QPushButton { background:#1e2a3a; color:#6699cc; border:1px solid #2a3a5a; "
        "border-radius:5px; font-size:12px; padding:0 14px; }"
        "QPushButton:hover { background:#2a3a4a; }");
    connect(importBtn, &QPushButton::clicked, this, &MainWindow::onImportClicked);

    cornerLayout->addWidget(exportBtn);
    cornerLayout->addWidget(importBtn);
    m_tabs->setCornerWidget(cornerWidget, Qt::TopRightCorner);

    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int newTab) {
        for (TileWidget* tw : std::as_const(m_tileWidgets))
            if (tabForTile(tw->tileData()) == newTab) tw->tick(true);
    });

    m_bottomBar = new QWidget(central);
    m_bottomBar->setStyleSheet("background-color:#0d0d0d; border-top:1px solid #2a2a2a;");
    m_bottomBar->setFixedHeight(80);
    auto* bl = new QVBoxLayout(m_bottomBar);
    bl->setContentsMargins(14, 10, 14, 4);
    bl->setSpacing(4);

    auto* inputRow = new QHBoxLayout;
    inputRow->setSpacing(10);

    m_searchEdit = new QLineEdit(m_bottomBar);
    m_searchEdit->setPlaceholderText(
        "Type a movie or show name…   e.g.  Invincible,  Supergirl 2026,  The Batman 2022");
    m_searchEdit->setMinimumHeight(48);
    m_searchEdit->setStyleSheet(
        "QLineEdit { background:#1e1e1e; color:#fff; border:1px solid #3a3a3a; "
        "border-radius:6px; padding:0 14px; font-size:16px; }"
        "QLineEdit:focus { border-color:#0078d4; }");
    connect(m_searchEdit, &QLineEdit::returnPressed, this, &MainWindow::onSearchClicked);
    connect(m_searchEdit, &QLineEdit::textEdited, this, [this](const QString& txt){
        if (txt.trimmed().isEmpty()) hidePicker();
    });
    inputRow->addWidget(m_searchEdit, 1);

    m_searchBtn = new QPushButton("🔍  Search Media", m_bottomBar);
    m_searchBtn->setFixedSize(170, 48);
    m_searchBtn->setStyleSheet(
        "QPushButton { background:#0078d4; color:#fff; border:none; border-radius:6px; "
        "font-size:15px; }"
        "QPushButton:hover { background:#1a8de4; }"
        "QPushButton:disabled { background:#2a2a2a; color:#555; }");
    connect(m_searchBtn, &QPushButton::clicked, this, &MainWindow::onSearchClicked);
    inputRow->addWidget(m_searchBtn);

    auto* customBtn = new QPushButton("✦  Custom Tile", m_bottomBar);
    customBtn->setFixedSize(140, 48);
    customBtn->setStyleSheet(
        "QPushButton { background:#2a2a4a; color:#aaaaee; border:1px solid #4a4a7a; "
        "border-radius:6px; font-size:15px; }"
        "QPushButton:hover { background:#3a3a6a; }");
    connect(customBtn, &QPushButton::clicked, this, &MainWindow::onCustomTileClicked);
    inputRow->addWidget(customBtn);
    bl->addLayout(inputRow);

    m_statusLbl = new QLabel("", m_bottomBar);
    m_statusLbl->setStyleSheet("color:#666; font-size:11px; background:transparent;");
    m_statusLbl->setFixedHeight(14);
    bl->addWidget(m_statusLbl);
    mainLayout->addWidget(m_bottomBar);

    m_pickerFrame = new QWidget(central);
    m_pickerFrame->setStyleSheet(
        "QWidget { background:#1e1e1e; border:1px solid #3a3a3a; border-radius:6px; }");
    m_pickerFrame->hide();
    auto* pfl = new QVBoxLayout(m_pickerFrame);
    pfl->setContentsMargins(0,0,0,0);
    pfl->setSpacing(0);

    // Dismiss button — arrow pointing down, sits at the top of the popup
    auto* dismissBtn = new QPushButton("▼  Dismiss", m_pickerFrame);
    dismissBtn->setFixedHeight(28);
    dismissBtn->setStyleSheet(
        "QPushButton { background:#2a2a2a; color:#888888; border:none; "
        "border-bottom:1px solid #3a3a3a; font-size:12px; }"
        "QPushButton:hover { background:#333333; color:#aaaaaa; }");
    connect(dismissBtn, &QPushButton::clicked, this, &MainWindow::hidePicker);
    pfl->addWidget(dismissBtn);

    m_pickerList = new QListWidget(m_pickerFrame);
    m_pickerList->setStyleSheet(
        "QListWidget { background:#1e1e1e; border:none; color:#fff; font-size:13px; }"
        "QListWidget::item { padding:10px 14px; border-bottom:1px solid #2a2a2a; }"
        "QListWidget::item:selected, QListWidget::item:hover { background:#0078d4; }");
    m_pickerList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_pickerList->setVerticalScrollMode(QAbstractItemView::ScrollPerItem);
    m_pickerList->setWordWrap(true);
    m_pickerList->installEventFilter(this);
    connect(m_pickerList, &QListWidget::itemClicked, this, &MainWindow::onPickerItemActivated);
    pfl->addWidget(m_pickerList);
    central->installEventFilter(this);

    m_globalTick = new QTimer(this);
    m_globalTick->setInterval(1000);
    m_globalTick->setTimerType(Qt::PreciseTimer);
    connect(m_globalTick, &QTimer::timeout, this, &MainWindow::onGlobalTick);
    m_globalTick->start();

    setupDebugWindow();

    // Hidden tray icon used exclusively by fireDirectNotification() for test
    // notifications fired directly from the main app (no IPC to TrayApp).
    // NOTE: intentionally NOT calling show() here — we only show it briefly
    // when actually firing a notification, then hide it again. Showing it at
    // startup creates a phantom second tray icon alongside the real notifier.
    m_testTray = new QSystemTrayIcon(
        QApplication::style()->standardIcon(QStyle::SP_MediaPlay), this);

    loadTiles();
    // Check for updates 3 seconds after launch so UI is fully ready
    QTimer::singleShot(3000, this, &MainWindow::checkForUpdates);
}

MainWindow::~MainWindow() { saveTiles(); }

bool MainWindow::eventFilter(QObject* obj, QEvent* event)
{
    if (event->type() == QEvent::Wheel) {
        // Picker list: always scroll exactly 1 item per wheel tick
        if (obj == m_pickerList->viewport()) {
            auto* we = static_cast<QWheelEvent*>(event);
            int dir = we->angleDelta().y() > 0 ? -1 : 1;
            int cur = m_pickerList->verticalScrollBar()->value();
            m_pickerList->verticalScrollBar()->setValue(cur + dir);
            return true;
        }
        for (int t = 0; t < 3; ++t) {
            if (m_scrollAreas[t] && obj == m_scrollAreas[t]->viewport()) {
                auto* we = static_cast<QWheelEvent*>(event);
                int pixels = -(we->angleDelta().y() * 120) / 120;
                smoothScrollBy(m_scrollAreas[t], pixels);
                return true;
            }
        }
    }
    if (event->type() == QEvent::MouseButtonPress && m_pickerFrame->isVisible()) {
        auto* me = static_cast<QMouseEvent*>(event);
        if (!m_pickerFrame->geometry().contains(me->pos())) hidePicker();
    }
    return QMainWindow::eventFilter(obj, event);
}

void MainWindow::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);
    if (m_pickerFrame->isVisible()) repositionPicker();
}

void MainWindow::repositionPicker()
{
    if (!m_pickerFrame || !m_bottomBar) return;
    static constexpr int VISIBLE = 6, DISMISS_H = 28;
    int itemH  = m_pickerList->count() > 0 ? m_pickerList->sizeHintForRow(0) : 44;
    int rows   = qMin(m_pickerList->count(), VISIBLE);
    if (rows < 1) rows = 1;
    int listH  = itemH * rows;
    int h      = listH + DISMISS_H + 2;
    int margin = 6;
    int w      = centralWidget()->width() - margin * 2;
    int y      = m_bottomBar->geometry().top() - h - 4;
    m_pickerFrame->setGeometry(margin, y, w, h);
    m_pickerList->setFixedHeight(listH);
}

void MainWindow::loadTiles()
{
    m_tiles = JsonManager::instance().loadTiles();
    APPLOG(QString("loadTiles: loaded %1 tile(s) from JSON").arg(m_tiles.size()));
    cleanupOrphanedFiles();
    std::sort(m_tiles.begin(), m_tiles.end(), tileOrder);
    for (const TileData& td : std::as_const(m_tiles))
        appendTileWidget(td);
    m_statusLbl->setText(
        QString("%1 tile%2 — refreshing…")
            .arg(m_tiles.size()).arg(m_tiles.size() == 1 ? "" : "s"));
    refreshAllTiles();
}

void MainWindow::saveTiles()
{
    JsonManager::instance().saveTiles(m_tiles);
    notifyTrayApp();
}

void MainWindow::refreshAllTiles()
{
    m_refreshPending = 0;
    for (const TileData& td : std::as_const(m_tiles))
        if (td.tmdbId > 0) { ++m_refreshPending; m_scraper->refreshTile(td); }
    if (m_refreshPending == 0)
        m_statusLbl->setText(
            QString("%1 tile%2 ready").arg(m_tiles.size()).arg(m_tiles.size()==1?"":"s"));
}

void MainWindow::appendTileWidget(const TileData& data)
{
    APPLOG(QString("appendTileWidget: '%1' → tab %2 | date=%3 | notif=%4")
           .arg(data.displayTitle())
           .arg(tabForTile(data))
           .arg(data.effectiveDate().toString("yyyy-MM-dd"))
           .arg(data.notifStatus == NotifStatus::Active ? "Active"
              : data.notifStatus == NotifStatus::Ready  ? "Ready" : "Inactive"));
    auto* tw = new TileWidget(data, nullptr);
    connect(tw, &TileWidget::imageChanged,     this, &MainWindow::onImageChanged);
    connect(tw, &TileWidget::tileDataChanged,  this, &MainWindow::onTileDataChanged);
    connect(tw, &TileWidget::removeTile,       this, &MainWindow::onRemoveTile);
    connect(tw, &TileWidget::duplicateTile,    this, &MainWindow::onDuplicateTile);
    connect(tw, &TileWidget::refetchRequested, this, &MainWindow::onRefetchRequested);
    connect(tw, &TileWidget::testNotification, this, &MainWindow::onTestNotification);
    m_tileWidgets.append(tw);
    sortAndRebuildAllTabs();
}

// =============================================================================
//  rebuildTab — clears and repopulates one tab's grid in sorted order.
//
//  KEY: clears ALL row stretch factors before setting the new bottom one.
//  This is what prevents ghost empty rows when tiles are deleted.
// =============================================================================
void MainWindow::rebuildTab(int tab)
{
    // Detach all widgets from the grid (does NOT delete them)
    QLayoutItem* item;
    while ((item = m_grids[tab]->takeAt(0)) != nullptr) {
        if (item->widget())
            item->widget()->setParent(nullptr);
        delete item;
    }

    // Collect and sort tiles for this tab
    QList<TileWidget*> inTab;
    for (TileWidget* tw : std::as_const(m_tileWidgets))
        if (tabForTile(tw->tileData()) == tab) inTab.append(tw);

    std::sort(inTab.begin(), inTab.end(), [this, tab](TileWidget* a, TileWidget* b){
        if (tab == TAB_OTHER) {
            // Preserve insertion order: tile added first appears first
            return m_tileWidgets.indexOf(a) < m_tileWidgets.indexOf(b);
        }
        if (tab == TAB_RELEASED)
            return tileOrder(b->tileData(), a->tileData()); // newest first
        return tileOrder(a->tileData(), b->tileData());      // soonest first
    });

    // Re-add in order
    for (int i = 0; i < inTab.size(); ++i) {
        inTab[i]->setParent(m_tabContainers[tab]);
        m_grids[tab]->addWidget(inTab[i], i / COLS, i % COLS);
        inTab[i]->show();
    }

    // Clear ALL previous row stretch factors, then set one below the last row.
    // Without clearing old stretches, Qt keeps allocating space for rows that
    // no longer have widgets — creating the ghost empty row after deletion.
    int totalRows = m_grids[tab]->rowCount();
    for (int r = 0; r < totalRows; ++r)
        m_grids[tab]->setRowStretch(r, 0);
    int lastRow = inTab.isEmpty() ? 0 : (inTab.size() - 1) / COLS;
    m_grids[tab]->setRowStretch(lastRow + 1, 1);
}

void MainWindow::sortAndRebuildAllTabs()
{
    for (int t = 0; t < 3; ++t) rebuildTab(t);
}

void MainWindow::onCustomTileClicked()
{
    CustomTileDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted) return;
    TileData td = dlg.result();
    m_tiles.append(td);
    appendTileWidget(td);
    saveTiles();
    m_statusLbl->setStyleSheet("color:#66bb66; font-size:11px;");
    m_statusLbl->setText(QString("Custom tile created: %1").arg(td.title));
}

void MainWindow::onSearchClicked()
{
    QString q = m_searchEdit->text().trimmed();
    if (q.isEmpty()) {
        m_statusLbl->setStyleSheet("color:#cc5555; font-size:11px;");
        m_statusLbl->setText("Type a movie or show name first.");
        return;
    }
    hidePicker();
    setInputBusy(true);
    m_statusLbl->setStyleSheet("color:#6688cc; font-size:11px;");
    m_statusLbl->setText("Searching TMDB…");
    m_scraper->searchMedia(q);
}

void MainWindow::onSearchResultsReady(const QList<SearchResult>& results)
{
    setInputBusy(false);
    m_statusLbl->setStyleSheet("color:#66aa66; font-size:11px;");
    m_statusLbl->setText(
        QString("Found %1 result%2 — pick one from above")
            .arg(results.size()).arg(results.size()==1?"":"s"));
    showPicker(results);
    m_scraper->fetchCreditsForResults(results);
}

void MainWindow::showPicker(const QList<SearchResult>& results)
{
    m_currentResults = results;
    m_pickerList->clear();
    for (const SearchResult& sr : results) {
        QString label = sr.title + "  •  " + (sr.mediaType=="tv" ? "TV Show" : "Movie");
        if (!sr.director.isEmpty()) label += "  •  " + sr.director;
        if (!sr.castLine.isEmpty()) label += "  •  " + sr.castLine;
        auto* item = new QListWidgetItem(label, m_pickerList);
        item->setData(Qt::UserRole, sr.id);
    }
    repositionPicker();
    m_pickerFrame->show();
    m_pickerFrame->raise();
}

void MainWindow::hidePicker()
{
    m_pickerFrame->hide();
    m_pickerList->clearSelection();
}

void MainWindow::onCreditsReady(int tmdbId, const QString& director, const QString& castLine)
{
    int idx = -1;
    for (int i = 0; i < m_currentResults.size(); ++i)
        if (m_currentResults[i].id == tmdbId) { idx = i; break; }
    if (idx < 0 || idx >= m_pickerList->count()) return;
    m_currentResults[idx].director = director;
    m_currentResults[idx].castLine = castLine;
    const SearchResult& sr = m_currentResults[idx];
    QString label = sr.title + "  •  " + (sr.mediaType=="tv"?"TV Show":"Movie");
    if (!sr.director.isEmpty()) label += "  •  " + sr.director;
    if (!sr.castLine.isEmpty()) label += "  •  " + sr.castLine;
    m_pickerList->item(idx)->setText(label);
}

void MainWindow::onPickerItemActivated(QListWidgetItem* item)
{
    int idx = m_pickerList->row(item);
    if (idx < 0 || idx >= m_currentResults.size()) return;
    hidePicker();
    const SearchResult& sr = m_currentResults[idx];
    for (const TileData& td : std::as_const(m_tiles)) {
        if (td.tmdbId == sr.id && td.tmdbId > 0) {
            m_statusLbl->setStyleSheet("color:#cc8833; font-size:11px;");
            m_statusLbl->setText(QString("Already added: %1").arg(td.displayTitle()));
            m_searchEdit->clear();
            return;
        }
    }
    m_searchEdit->setText(sr.title);
    setInputBusy(true);
    m_statusLbl->setStyleSheet("color:#6688cc; font-size:11px;");
    m_statusLbl->setText(QString("Fetching details for %1…").arg(sr.title));
    m_scraper->fetchDetails(sr.id, sr.mediaType, sr.posterPath);
}

void MainWindow::onScraperDataReady(const TileData& data)
{
    setInputBusy(false);
    m_searchEdit->clear();
    TileData td = data;
    if (td.presetType.isEmpty()) td.presetType = "Media";
    m_tiles.append(td);
    appendTileWidget(td);
    saveTiles();
    m_statusLbl->setStyleSheet("color:#66bb66; font-size:11px;");
    m_statusLbl->setText(QString("Added: %1").arg(td.title));
}

void MainWindow::onTileRefreshed(const TileData& updated)
{
    for (TileData& td : m_tiles) {
        if (td.id != updated.id) continue;
        bool tmdbTitleChanged = (updated.title != td.title);
        QString savedCustomTitle   = td.customTitle;
        QDate   savedCustomDate    = td.customDate;
        QString savedCustomDateStr = td.customDateStr;
        QTime   savedCustomAirTime = td.customAirTime;
        QString savedImagePath     = td.imagePath;
        NotifStatus savedNotif     = td.notifStatus;
        bool    savedNoDate        = td.noDateOverride;
        bool    savedIsLooped      = td.isLooped;
        QString savedPresetType    = td.presetType;
        QString savedLoopInterval  = td.loopInterval;
        int     savedLoopWeekday   = td.loopWeekday;
        int     savedLoopDayOfMonth= td.loopDayOfMonth;
        td = updated;
        td.customDate      = savedCustomDate;
        td.customDateStr   = savedCustomDateStr;
        td.customAirTime   = savedCustomAirTime;
        td.imagePath       = savedImagePath;
        td.customTitle     = tmdbTitleChanged ? QString() : savedCustomTitle;
        td.noDateOverride  = savedNoDate;
        td.isLooped        = savedIsLooped;
        td.presetType      = savedPresetType;
        td.loopInterval    = savedLoopInterval;
        td.loopWeekday     = savedLoopWeekday;
        td.loopDayOfMonth  = savedLoopDayOfMonth;

        // If TMDB returned a NEW future targetDate (e.g. next episode of a TV show),
        // reset the notif status so the tile moves back to Countdowns and counts down
        bool dateAdvanced = updated.targetDate.isValid() &&
                            !(updated.targetDate < QDate::currentDate()) &&
                            (updated.targetDate != savedCustomDate ||
                             savedNotif == NotifStatus::Ready);

        if (dateAdvanced) {
            td.notifStatus = NotifStatus::Active;
            td.notified    = false;
        } else {
            td.notifStatus = savedNotif;
            // Keep notified=true only if the TMDB date hasn't changed
            td.notified    = (td.targetDate == updated.targetDate) && (savedNotif != NotifStatus::Active);
        }

        for (TileWidget* tw : std::as_const(m_tileWidgets)) {
            if (tw->tileData().id == td.id) {
                int oldTab = tabForTile(tw->tileData());
                tw->updateData(td);
                int newTab = tabForTile(td);
                if (oldTab != newTab) {
                    m_grids[oldTab]->removeWidget(tw);
                    tw->setParent(m_tabContainers[newTab]);
                    rebuildTab(oldTab);
                    rebuildTab(newTab);
                }
                break;
            }
        }
        break;
    }
    if (--m_refreshPending <= 0) {
        sortAndRebuildAllTabs();
        m_statusLbl->setText(
            QString("%1 tile%2 ready").arg(m_tiles.size()).arg(m_tiles.size()==1?"":"s"));
        saveTiles();
    }
}

void MainWindow::onPosterReady(const QString& tileId, const QString& localPath)
{
    auto isOwned = [](const QString& p) {
        return !p.isEmpty() &&
               (p.contains("/fetched_images/") || p.contains("\\fetched_images\\") ||
                p.contains("/custom_images/")  || p.contains("\\custom_images\\"));
    };
    for (TileData& td : m_tiles) {
        if (td.id != tileId) continue;
        if (td.imagePath != localPath && isOwned(td.imagePath)) QFile::remove(td.imagePath);
        td.imagePath = localPath;
        break;
    }
    for (TileWidget* tw : std::as_const(m_tileWidgets)) {
        if (tw->tileData().id == tileId) {
            TileData upd = tw->tileData(); upd.imagePath = localPath;
            tw->updateData(upd); break;
        }
    }
    saveTiles();
}

void MainWindow::onScraperError(const QString& msg)
{
    setInputBusy(false);
    m_statusLbl->setStyleSheet("color:#cc5555; font-size:11px;");
    m_statusLbl->setText("Error: " + msg.split('\n').first());
}

void MainWindow::setInputBusy(bool busy)
{
    m_searchEdit->setEnabled(!busy);
    m_searchBtn->setEnabled(!busy);
    m_searchBtn->setText(busy ? "Searching…" : "🔍  Search Media");
}

void MainWindow::onTileDataChanged(const QString& tileId)
{
    for (TileWidget* tw : std::as_const(m_tileWidgets)) {
        if (tw->tileData().id != tileId) continue;
        const TileData& w = tw->tileData();
        for (TileData& td : m_tiles) {
            if (td.id != tileId) continue;
            int oldTab = tabForTile(td);
            td.customTitle    = w.customTitle;
            td.customDate     = w.customDate;
            td.customDateStr  = w.customDateStr;
            td.customAirTime  = w.customAirTime;
            td.imagePath      = w.imagePath;
            td.noDateOverride = w.noDateOverride;
            td.isLooped       = w.isLooped;
            td.loopInterval   = w.loopInterval;
            td.loopWeekday    = w.loopWeekday;
            td.loopDayOfMonth = w.loopDayOfMonth;
            td.presetType     = w.presetType;
            int newTab = tabForTile(td);

            // KEY FIX: when a tile moves from Released back to Active (user
            // set a future date), reset notifStatus to Active so onGlobalTick
            // will fire the transition and notification again when it expires.
            // Without this, notifStatus stays Ready and the tile never moves
            // tabs on its own — and deleting it causes an empty space because
            // tabForTile returns TAB_RELEASED (isExpired=true) while the widget
            // is physically sitting in TAB_ACTIVE's grid.
            if ((oldTab == TAB_RELEASED && newTab == TAB_ACTIVE) ||
                (oldTab == TAB_RELEASED && newTab == TAB_OTHER)) {
                td.notifStatus = NotifStatus::Active;
                tw->updateData(td);
                APPLOG(QString("onTileDataChanged: '%1' released→%2").arg(td.displayTitle(), newTab == TAB_ACTIVE ? "active" : "other"));
            }

            if (oldTab != newTab) {
                m_grids[oldTab]->removeWidget(tw);
                tw->setParent(m_tabContainers[newTab]);
                rebuildTab(oldTab);
                rebuildTab(newTab);
            } else {
                // Same tab but data changed (e.g. date shifted within Active) —
                // rebuild so the tile moves to its correct sorted position.
                rebuildTab(oldTab);
            }
            break;
        }
        break;
    }
    saveTiles();
}

void MainWindow::onImageChanged(const QString& tileId, const QString& /*path*/)
{
    auto isOwned = [](const QString& p) {
        return !p.isEmpty() &&
               (p.contains("/fetched_images/") || p.contains("\\fetched_images\\") ||
                p.contains("/custom_images/")  || p.contains("\\custom_images\\"));
    };
    for (TileWidget* tw : std::as_const(m_tileWidgets)) {
        if (tw->tileData().id != tileId) continue;
        const TileData& wData = tw->tileData();
        for (TileData& td : m_tiles) {
            if (td.id != tileId) continue;
            QString oldPath = td.imagePath;
            if (oldPath != wData.imagePath && isOwned(oldPath)) QFile::remove(oldPath);
            td.imagePath     = wData.imagePath;
            td.customTitle   = wData.customTitle;
            td.customDate    = wData.customDate;
            td.customDateStr = wData.customDateStr;
            td.customAirTime = wData.customAirTime;
            break;
        }
        break;
    }
    saveTiles();
}

// =============================================================================
//  onRemoveTile — removes tile from data, destroys widget synchronously,
//  then rebuilds the tab so remaining tiles fill the gap immediately.
//
//  IMPORTANT: delete tw synchronously (not deleteLater) so the widget is
//  fully gone before rebuildTab runs. deleteLater defers destruction to the
//  next event loop — the widget would still exist as a hidden child of the
//  container during rebuildTab, causing crashes or ghost gaps.
// =============================================================================
void MainWindow::onRemoveTile(const QString& tileId)
{
    APPLOG(QString("onRemoveTile: id=%1").arg(tileId));
    QString imgToDelete;
    for (const TileData& td : std::as_const(m_tiles)) {
        if (td.id == tileId) {
            if (!td.imagePath.isEmpty() &&
                (td.imagePath.contains("/fetched_images/") || td.imagePath.contains("\\fetched_images\\") ||
                 td.imagePath.contains("/custom_images/")  || td.imagePath.contains("\\custom_images\\")))
                imgToDelete = td.imagePath;
            break;
        }
    }
    m_tiles.removeIf([&](const TileData& td){ return td.id == tileId; });

    for (int i = 0; i < m_tileWidgets.size(); ++i) {
        if (m_tileWidgets[i]->tileData().id == tileId) {
            TileWidget* tw = m_tileWidgets.takeAt(i);
            int tab = tabForTile(tw->tileData());
            APPLOG(QString("onRemoveTile: removing widget from tab %1, rebuilding").arg(tab));
            m_grids[tab]->removeWidget(tw);  // detach from layout
            tw->setParent(nullptr);           // unparent so delete is clean
            delete tw;                        // synchronous — gone before rebuildTab
            rebuildTab(tab);                  // reflow remaining tiles
            break;
        }
    }

    if (!imgToDelete.isEmpty()) QFile::remove(imgToDelete);
    saveTiles();
    m_statusLbl->setStyleSheet("color:#888; font-size:11px;");
    m_statusLbl->setText("Tile removed.");
}

void MainWindow::onDuplicateTile(const QString& tileId)
{
    // Find the source tile
    TileData src;
    bool found = false;
    for (const TileData& td : std::as_const(m_tiles)) {
        if (td.id == tileId) { src = td; found = true; break; }
    }
    if (!found) return;

    // Give the duplicate a fresh id and reset notification state
    src.id         = QUuid::createUuid().toString(QUuid::WithoutBraces);
    src.notifStatus = NotifStatus::Active;
    src.notified   = false;

    m_tiles.append(src);
    appendTileWidget(src);
    saveTiles();
    APPLOG(QString("onDuplicateTile: duplicated '%1' → new id %2").arg(src.displayTitle(), src.id));
    m_statusLbl->setStyleSheet("color:#66bb66; font-size:11px;");
    m_statusLbl->setText(QString("Duplicated: %1").arg(src.displayTitle()));
}

// =============================================================================
//  onGlobalTick — fires every second.
//
//  1. Ticks countdowns on the visible tab.
//  2. Detects Active tiles that just expired → moves them to Released tab
//     immediately (no restart required) + marks Ready for notifier.
// =============================================================================
void MainWindow::onGlobalTick()
{
    int currentTab = m_tabs->currentIndex();
    bool anyExpired = false;

    for (TileWidget* tw : std::as_const(m_tileWidgets)) {
        bool onVisibleTab = (tabForTile(tw->tileData()) == currentTab);
        tw->tick(onVisibleTab);

        // Detect Active → expired transition
        const TileData& td = tw->tileData();
        if (td.notifStatus == NotifStatus::Active && td.hasDate() && td.isExpired()) {
            for (TileData& mtd : m_tiles) {
                if (mtd.id != td.id) continue;

                // Fire notification regardless of loop state
                fireDirectNotification(mtd);

                if (mtd.isLooped) {
                    APPLOG(QString("onGlobalTick: looped tile '%1' expired — advancing (%2)").arg(mtd.displayTitle(), mtd.loopInterval));
                    QDate next;
                    QDate today = QDate::currentDate().addDays(1);
                    QString interval = mtd.loopInterval.isEmpty() ? "Yearly" : mtd.loopInterval;

                    if (interval == "Daily") {
                        next = QDate::currentDate().addDays(1);
                    } else if (interval == "Weekly") {
                        int wd = mtd.loopWeekday;
                        int daysAhead = (wd - today.dayOfWeek() + 7) % 7;
                        if (daysAhead == 0) daysAhead = 7;
                        next = today.addDays(daysAhead);
                    } else if (interval == "Monthly") {
                        int dom = qMax(1, qMin(mtd.loopDayOfMonth, 28));
                        next = QDate(today.year(), today.month(), dom);
                        if (next <= QDate::currentDate()) next = next.addMonths(1);
                        // Clamp to valid days in that month
                        next = QDate(next.year(), next.month(), qMin(dom, next.daysInMonth()));
                    } else { // Yearly
                        QString preset = mtd.presetType;
                        if (preset == "Easter") {
                            int y = today.year();
                            auto easter = [](int yr) {
                                int a=yr%19,b=yr/100,c=yr%100,d=b/4,e=b%4,f=(b+8)/25;
                                int g=(b-f+1)/3,h=(19*a+b-d-g+15)%30,i=c/4,k=c%4;
                                int l=(32+2*e+2*i-h-k)%7,m=(a+11*h+22*l)/451;
                                return QDate(yr,(h+l-7*m+114)/31,((h+l-7*m+114)%31)+1);
                            };
                            next = easter(y); if (next < today) next = easter(y+1);
                        } else if (preset == "Good Friday") {
                            int y = today.year();
                            auto easter = [](int yr) {
                                int a=yr%19,b=yr/100,c=yr%100,d=b/4,e=b%4,f=(b+8)/25;
                                int g=(b-f+1)/3,h=(19*a+b-d-g+15)%30,i=c/4,k=c%4;
                                int l=(32+2*e+2*i-h-k)%7,m=(a+11*h+22*l)/451;
                                return QDate(yr,(h+l-7*m+114)/31,((h+l-7*m+114)%31)+1);
                            };
                            next = easter(y).addDays(-2); if (next < today) next = easter(y+1).addDays(-2);
                        } else {
                            QDate base = mtd.targetDate.isValid() ? mtd.targetDate : mtd.customDate;
                            next = base.addYears(1);
                            if (!next.isValid())
                                next = QDate(base.year()+1, base.month(), qMin(base.day(), QDate(base.year()+1, base.month(), 1).daysInMonth()));
                        }
                    }
                    if (next.isValid()) {
                        mtd.targetDate    = next;
                        mtd.customDate    = QDate();
                        mtd.dateDisplay   = next.toString("MMMM d, yyyy");
                        mtd.customDateStr = "";
                        mtd.notifStatus   = NotifStatus::Active;
                        mtd.notified      = false;
                        tw->updateData(mtd);
                        anyExpired = true; // triggers rebuildTab so display refreshes
                    }
                } else {
                    APPLOG(QString("onGlobalTick: tile '%1' just expired — moving to Released tab").arg(mtd.displayTitle()));
                    mtd.notifStatus = NotifStatus::Ready;
                    tw->updateData(mtd);
                    anyExpired = true;
                }
                break;
            }
        }
    }

    if (anyExpired) {
        // Rebuild both tabs so the tile visually moves from Countdowns → Released
        rebuildTab(TAB_ACTIVE);
        rebuildTab(TAB_RELEASED);
        saveTiles();
        notifyTrayApp();
    }
}

void MainWindow::onRefetchRequested(const QString& tileId)
{
    for (const TileData& td : std::as_const(m_tiles))
        if (td.id == tileId) { ++m_refreshPending; m_scraper->refreshTile(td); return; }
}

// =============================================================================
//  onTestNotification — fires a Windows notification directly from the main
//  app via m_testTray (a hidden QSystemTrayIcon).
//
//  This deliberately bypasses the TrayApp IPC so we can independently verify:
//    (a) whether QSystemTrayIcon::showMessage works at all on this machine, and
//    (b) whether any failure is in the notification method vs. the IPC channel.
// =============================================================================
void MainWindow::onTestNotification(const QString& tileId)
{
    APPLOG(QString("onTestNotification: tileId=%1 — firing DIRECT notification from main app").arg(tileId));

    // Find the tile data
    const TileData* found = nullptr;
    for (const TileData& td : std::as_const(m_tiles)) {
        if (td.id == tileId) { found = &td; break; }
    }
    if (!found) {
        APPLOG("onTestNotification: tile not found in m_tiles!");
        m_statusLbl->setStyleSheet("color:#cc5555; font-size:11px;");
        m_statusLbl->setText("Test notification failed — tile not found.");
        return;
    }

    fireDirectNotification(*found);

    m_statusLbl->setStyleSheet("color:#66aa66; font-size:11px;");
    m_statusLbl->setText("Test notification fired directly from main app.");
}

// =============================================================================
//  fireDirectNotification — fires a QSystemTrayIcon::showMessage notification
//  from the main app's own hidden tray icon (m_testTray).
//  Same logic as TrayApp::sendNotification so results are directly comparable.
// =============================================================================
void MainWindow::fireDirectNotification(const TileData& td)
{
    // Strip the "  •  N Seasons" suffix from the title.
    // td.title is stored as e.g. "Invincible (2021)  •  4 Seasons".
    // For notifications we only want the bare show/movie name.
    QString title = td.displayTitle();
    int bullet = title.indexOf(QChar(0x2022));  // Unicode bullet •
    if (bullet >= 0) title = title.left(bullet).trimmed();

    // Build the notification body — show name is already the toast title,
    // so the body only contains the episode/status info.
    //
    //  statusLabel   →  body
    //  ""            →  "Now available!"
    //  "Releases"    →  "Now available!"
    //  "Released"    →  "Now available!"
    //  "S02E01"      →  "S02E01 is out!"
    //  "Last Episode"→  "Last Episode is out!"
    //  "S02E01+E02"  →  "S02E01+E02 is out!"
    QString body;
    if (td.statusLabel.isEmpty()
        || td.statusLabel == "Releases"
        || td.statusLabel == "Released"
        || td.statusLabel == "No Release Date Yet")
    {
        body = "Now available!";
    } else {
        body = QString("%1 is out!").arg(td.statusLabel);
    }

    APPLOG(QString("fireDirectNotification: title='%1' body='%2'").arg(title, body));

    // Build icon from tile's backdrop image (same as TrayApp does)
    QIcon notifIcon;
    if (!td.imagePath.isEmpty() && QFile::exists(td.imagePath)) {
        QPixmap px(td.imagePath);
        if (!px.isNull()) {
            notifIcon = QIcon(px.scaled(256, 144, Qt::KeepAspectRatio, Qt::SmoothTransformation));
            APPLOG("fireDirectNotification: using tile backdrop as icon");
        }
    }
    if (notifIcon.isNull()) {
        notifIcon = QApplication::style()->standardIcon(QStyle::SP_MediaPlay);
        APPLOG("fireDirectNotification: using default icon (no backdrop)");
    }

    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        APPLOG("fireDirectNotification: ERROR — system tray is NOT available on this system");
        m_statusLbl->setStyleSheet("color:#cc5555; font-size:11px;");
        m_statusLbl->setText("System tray not available — cannot show notification.");
        return;
    }
    if (!QSystemTrayIcon::supportsMessages()) {
        APPLOG("fireDirectNotification: ERROR — system tray does NOT support messages on this system");
        m_statusLbl->setStyleSheet("color:#cc5555; font-size:11px;");
        m_statusLbl->setText("System tray does not support notifications on this system.");
        return;
    }

    // Swap icon temporarily so it shows as the notification icon
    m_testTray->setIcon(notifIcon);
    // show() is required for showMessage() to work; we hide after the
    // notification duration so no persistent extra tray icon is visible.
    m_testTray->show();
    m_testTray->showMessage(title, body, notifIcon, 10000);
    APPLOG("fireDirectNotification: showMessage() called — notification should appear");

    // Hide and restore after notification timeout
    QTimer::singleShot(12000, this, [this]() {
        if (m_testTray) {
            m_testTray->hide();
            m_testTray->setIcon(
                QApplication::style()->standardIcon(QStyle::SP_MediaPlay));
        }
    });
}

// =============================================================================
//  onExportClicked — packages tiles.json + fetched_images + custom_images
//  into a zip and asks the user where to save it.
//
//  Uses PowerShell's Compress-Archive (Windows built-in, no extra deps).
// =============================================================================
void MainWindow::onExportClicked()
{
    QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);

    // Ask where to save
    QString dest = QFileDialog::getSaveFileName(
        this, "Export Tiles", QDir::homePath() + "/MediaCountdowns_export.zip",
        "Zip Archive (*.zip)");
    if (dest.isEmpty()) return;

    // Remove existing file at dest so PowerShell doesn't complain
    QFile::remove(dest);

    // Build list of items to include — tiles.json plus both image folders
    QStringList items;
    QString jsonPath = appData + "/tiles.json";
    if (QFile::exists(jsonPath))
        items << jsonPath;
    for (const QString& folder : {QString("fetched_images"), QString("custom_images")}) {
        QString p = appData + "/" + folder;
        if (QDir(p).exists()) items << p;
    }

    if (items.isEmpty()) {
        QMessageBox::information(this, "Export", "Nothing to export — no tiles found.");
        return;
    }

    // Build PowerShell command: Compress-Archive -Path a,b,c -DestinationPath dest
    QStringList psItems;
    for (const QString& it : items)
        psItems << "\"" + QDir::toNativeSeparators(it) + "\"";

    QString psCmd = QString(
        "Compress-Archive -Path %1 -DestinationPath \"%2\"")
        .arg(psItems.join(","))
        .arg(QDir::toNativeSeparators(dest));

    int ret = QProcess::execute("powershell",
        QStringList() << "-NoProfile" << "-Command" << psCmd);

    if (ret == 0 && QFile::exists(dest)) {
        m_statusLbl->setStyleSheet("color:#66bb66; font-size:11px;");
        m_statusLbl->setText(QString("Exported to: %1").arg(QFileInfo(dest).fileName()));
    } else {
        QMessageBox::warning(this, "Export Failed",
            "Could not create the zip file.\nMake sure PowerShell is available.");
    }
}

// =============================================================================
//  onImportClicked — merges tiles from a previously exported zip.
//
//  Rules:
//   • Tiles whose ID already exists in m_tiles are SKIPPED (no overwrite).
//   • Images are copied into the local fetched_images / custom_images folders.
//   • tiles.json from the zip is merged, not replaced.
// =============================================================================
void MainWindow::onImportClicked()
{
    QString zipPath = QFileDialog::getOpenFileName(
        this, "Import Tiles", QDir::homePath(),
        "Zip Archive (*.zip)");
    if (zipPath.isEmpty()) return;

    QString appData  = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QString tempDir  = appData + "/import_temp";

    // Clean up any previous temp extract
    QDir(tempDir).removeRecursively();
    QDir().mkpath(tempDir);

    // Extract zip via PowerShell
    QString psCmd = QString(
        "Expand-Archive -Path \"%1\" -DestinationPath \"%2\" -Force")
        .arg(QDir::toNativeSeparators(zipPath))
        .arg(QDir::toNativeSeparators(tempDir));

    int ret = QProcess::execute("powershell",
        QStringList() << "-NoProfile" << "-Command" << psCmd);

    if (ret != 0) {
        QDir(tempDir).removeRecursively();
        QMessageBox::warning(this, "Import Failed",
            "Could not extract the zip file.");
        return;
    }

    // Find tiles.json inside extracted folder (may be nested one level)
    QString importedJson;
    for (const QString& candidate : {
        tempDir + "/tiles.json",
        tempDir + "/" + QFileInfo(appData).fileName() + "/tiles.json"
    }) {
        if (QFile::exists(candidate)) { importedJson = candidate; break; }
    }
    // Fallback: search recursively
    if (importedJson.isEmpty()) {
        QDirIterator it(tempDir, QStringList() << "tiles.json",
                        QDir::Files, QDirIterator::Subdirectories);
        if (it.hasNext()) importedJson = it.next();
    }

    if (importedJson.isEmpty()) {
        QDir(tempDir).removeRecursively();
        QMessageBox::warning(this, "Import Failed",
            "No tiles.json found inside the zip.");
        return;
    }

    // Parse the imported JSON
    QFile jf(importedJson);
    if (!jf.open(QIODevice::ReadOnly)) {
        QDir(tempDir).removeRecursively();
        QMessageBox::warning(this, "Import Failed", "Could not read tiles.json.");
        return;
    }
    QJsonDocument doc = QJsonDocument::fromJson(jf.readAll());
    jf.close();
    if (!doc.isArray()) {
        QDir(tempDir).removeRecursively();
        QMessageBox::warning(this, "Import Failed", "tiles.json is not valid.");
        return;
    }

    // Build set of existing IDs for duplicate check
    QSet<QString> existingIds;
    for (const TileData& td : std::as_const(m_tiles))
        existingIds.insert(td.id);

    QString importedJsonDir = QFileInfo(importedJson).absolutePath();
    int added = 0, skipped = 0;

    for (const QJsonValue& v : doc.array()) {
        QJsonObject o = v.toObject();
        QString id = o["id"].toString();
        if (id.isEmpty() || existingIds.contains(id)) { ++skipped; continue; }

        // Copy image file if it exists in the extracted folder
        QString imgPath = o["imagePath"].toString();
        if (!imgPath.isEmpty()) {
            QString imgFilename = QFileInfo(imgPath).fileName();
            // Determine destination subfolder from original path
            QString subfolder = imgPath.contains("custom_images") ? "custom_images" : "fetched_images";
            QString destFolder = appData + "/" + subfolder;
            QDir().mkpath(destFolder);
            QString destPath = destFolder + "/" + imgFilename;

            // Try to find the image in extracted temp dir
            bool copied = false;
            for (const QString& sf : {QString("fetched_images"), QString("custom_images")}) {
                QString src = importedJsonDir + "/../" + sf + "/" + imgFilename;
                if (!QFile::exists(src))
                    src = importedJsonDir + "/" + sf + "/" + imgFilename;
                if (QFile::exists(src)) {
                    QFile::copy(src, destPath);
                    copied = true;
                    break;
                }
            }
            // Also try a flat search in tempDir
            if (!copied) {
                QDirIterator imgIt(tempDir, QStringList() << imgFilename,
                                   QDir::Files, QDirIterator::Subdirectories);
                if (imgIt.hasNext()) QFile::copy(imgIt.next(), destPath);
            }
            o["imagePath"] = destPath;
        }

        // Re-parse the (possibly patched) tile and add it
        TileData td;
        td.id            = id;
        td.title         = o["title"].toString();
        td.customTitle   = o["customTitle"].toString();
        td.tmdbId        = o["tmdbId"].toInt();
        td.mediaType     = o["mediaType"].toString();
        td.tmdbUrl       = o["tmdbUrl"].toString();
        td.statusLabel   = o["statusLabel"].toString();
        td.rawDateText   = o["rawDateText"].toString();
        td.dateDisplay   = o["dateDisplay"].toString();
        td.customDateStr = o["customDateStr"].toString();
        td.targetDate    = QDate::fromString(o["targetDate"].toString(), Qt::ISODate);
        td.customDate    = QDate::fromString(o["customDate"].toString(), Qt::ISODate);
        td.airTime       = QTime::fromString(o["airTime"].toString(), "HH:mm");
        int airMins      = o["customAirMins"].toInt(-1);
        td.customAirTime = (airMins >= 0 && airMins < 1440)
            ? QTime(airMins / 60, airMins % 60) : QTime();
        td.imagePath     = o["imagePath"].toString();
        QString ns       = o["notifStatus"].toString("Active");
        if (ns == "Inactive")   td.notifStatus = NotifStatus::Inactive;
        else if (ns == "Ready") td.notifStatus = NotifStatus::Ready;
        else                    td.notifStatus = NotifStatus::Active;

        if (td.statusLabel == "Returning Series" || td.statusLabel == "Ended")
            td.statusLabel = "Last Episode";

        m_tiles.append(td);
        existingIds.insert(id);
        appendTileWidget(td);
        ++added;
    }

    QDir(tempDir).removeRecursively();
    saveTiles();

    m_statusLbl->setStyleSheet("color:#66bb66; font-size:11px;");
    m_statusLbl->setText(QString("Import complete — %1 added, %2 skipped (duplicates).")
                         .arg(added).arg(skipped));

    if (added == 0)
        QMessageBox::information(this, "Import",
            QString("No new tiles were added.\n%1 tile(s) already existed.").arg(skipped));
}

void MainWindow::notifyTrayApp()
{
    QLocalSocket sock;
    sock.connectToServer("MediaCountdownsTray");
    if (sock.waitForConnected(300)) {
        sock.write("REFRESH\n");
        sock.waitForBytesWritten(300);
        sock.disconnectFromServer();
    }
}

void MainWindow::installSmoothScroll(QScrollArea* area)
{
    area->viewport()->installEventFilter(this);
}

void MainWindow::smoothScrollBy(QScrollArea* area, int pixelDelta)
{
    int tab = -1;
    for (int t = 0; t < 3; ++t)
        if (m_scrollAreas[t] == area) { tab = t; break; }
    if (tab < 0) return;

    QScrollBar* sb = area->verticalScrollBar();
    m_scrollTarget[tab] = qBound(sb->minimum(),
                                  m_scrollTarget[tab] + pixelDelta,
                                  sb->maximum());
    QVariantAnimation* anim = m_scrollAnim[tab];
    anim->stop();
    anim->setStartValue(sb->value());
    anim->setEndValue(m_scrollTarget[tab]);
    anim->start();
}

void MainWindow::cleanupOrphanedFiles()
{
    QSet<QString> inUse;
    for (const TileData& td : std::as_const(m_tiles))
        if (!td.imagePath.isEmpty()) inUse.insert(QFileInfo(td.imagePath).fileName());

    QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    for (const QString& folder : {QString("fetched_images"), QString("custom_images")}) {
        QDir dir(appData + "/" + folder);
        if (!dir.exists()) continue;
        for (const QString& file : dir.entryList(QDir::Files))
            if (!inUse.contains(file)) dir.remove(file);
    }
}

// =============================================================================
//  Debug window — Ctrl+Shift+D toggles a floating log window.
//  AppLogger::instance().log(msg) feeds into this window from anywhere in the app.
// =============================================================================
void MainWindow::setupDebugWindow()
{
    m_debugWindow = new QDialog(this,
        Qt::Window | Qt::WindowMinMaxButtonsHint | Qt::WindowCloseButtonHint);
    m_debugWindow->setWindowTitle("Debug Log — Media Countdowns  v2.2.1");
    m_debugWindow->resize(860, 480);
    m_debugWindow->setStyleSheet("background:#111; color:#ccc;");

    auto* vlay = new QVBoxLayout(m_debugWindow);
    vlay->setContentsMargins(4, 4, 4, 4);

    m_debugLog = new QPlainTextEdit(m_debugWindow);
    m_debugLog->setReadOnly(true);
    m_debugLog->setMaximumBlockCount(2000);
    m_debugLog->setStyleSheet(
        "QPlainTextEdit { background:#0a0a0a; color:#b0ffb0; "
        "font-family:'Consolas','Courier New',monospace; font-size:11px; "
        "border:1px solid #222; }");
    vlay->addWidget(m_debugLog);

    auto* btnRow = new QHBoxLayout;
    auto* clearBtn = new QPushButton("Clear", m_debugWindow);
    clearBtn->setStyleSheet(
        "QPushButton { background:#222; color:#aaa; border:1px solid #444; "
        "border-radius:4px; padding:4px 14px; } "
        "QPushButton:hover { background:#333; }");
    connect(clearBtn, &QPushButton::clicked, m_debugLog, &QPlainTextEdit::clear);
    btnRow->addWidget(clearBtn);
    btnRow->addStretch();
    auto* hintLbl = new QLabel("  Press  Ctrl+Shift+D  to toggle this window  ", m_debugWindow);
    hintLbl->setStyleSheet("color:#555; font-size:10px;");
    btnRow->addWidget(hintLbl);
    vlay->addLayout(btnRow);

    connect(&AppLogger::instance(), &AppLogger::newEntry,
            this, [this](const QString& entry) {
        if (m_debugLog) {
            m_debugLog->appendPlainText(entry);
            QTextCursor c = m_debugLog->textCursor();
            c.movePosition(QTextCursor::End);
            m_debugLog->setTextCursor(c);
        }
    });

    auto* sc = new QShortcut(QKeySequence("Ctrl+Shift+D"), this);
    connect(sc, &QShortcut::activated, this, &MainWindow::showDebugWindow);

    APPLOG("=== Debug log started ===");
    APPLOG(QString("App version: 2.2.1"));
    APPLOG(QString("Qt version: %1").arg(qVersion()));
}

void MainWindow::showDebugWindow()
{
    if (!m_debugWindow) return;
    if (m_debugWindow->isVisible()) {
        m_debugWindow->hide();
    } else {
        m_debugWindow->show();
        m_debugWindow->raise();
        m_debugWindow->activateWindow();
    }
}

// =============================================================================
void MainWindow::checkForUpdates()
{
    static const QString kCurrentVersion = "2.2.1";
    static const QString kApiUrl =
        "https://api.github.com/repos/HijackAssassin/MediaCountdowns/releases/latest";

    auto* nam = new QNetworkAccessManager(this);
    QNetworkRequest req(kApiUrl);
    req.setRawHeader("User-Agent", "MediaCountdowns");
    auto* reply = nam->get(req);

    connect(reply, &QNetworkReply::finished, this, [this, reply, nam]() {
        reply->deleteLater();
        nam->deleteLater();
        if (reply->error() != QNetworkReply::NoError) return;

        QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
        QString tag = obj["tag_name"].toString().trimmed();
        QString url = obj["html_url"].toString().trimmed();
        if (tag.isEmpty() || url.isEmpty()) return;

        // Strip any prefix: "Release-V2.2.0", "Release-v2.2.0", "v2.2.0", "V2.2.0" → "2.2.0"
        auto stripPrefix = [](QString s) -> QString {
            // Remove "release-" prefix (case insensitive)
            if (s.startsWith("release-", Qt::CaseInsensitive))
                s = s.mid(8);
            // Remove leading v/V
            if (!s.isEmpty() && (s[0] == 'v' || s[0] == 'V'))
                s = s.mid(1);
            return s.trimmed();
        };
        QString latestClean  = stripPrefix(tag);
        QString currentClean = stripPrefix(kCurrentVersion);

        // Parse as version numbers for correct numeric comparison
        auto parseVer = [](const QString& v) -> QList<int> {
            QList<int> parts;
            for (const QString& p : v.split('.')) parts << p.toInt();
            while (parts.size() < 3) parts << 0;
            return parts;
        };
        QList<int> lv = parseVer(latestClean);
        QList<int> cv = parseVer(currentClean);
        if (lv <= cv) return;  // already up to date

        // Check if user already skipped this version
        QSettings settings("HijackAssassin", "MediaCountdowns");
        QStringList skipped = settings.value("skippedUpdates").toStringList();
        if (skipped.contains(latestClean)) return;

        // Show update popup
        auto* dlg = new QDialog(this, Qt::Dialog);
        dlg->setWindowTitle("Update Available");
        dlg->setFixedWidth(400);
        dlg->setAttribute(Qt::WA_DeleteOnClose);

        auto* vlay = new QVBoxLayout(dlg);
        vlay->setContentsMargins(24, 24, 24, 24);
        vlay->setSpacing(12);

        auto* title = new QLabel(QString("A new version is available: <b>%1</b>").arg(tag), dlg);
        title->setStyleSheet("font-size:14px; color:#ffffff;");
        title->setWordWrap(true);
        vlay->addWidget(title);

        auto* sub = new QLabel("Would you like to download it?", dlg);
        sub->setStyleSheet("font-size:12px; color:#aaaaaa;");
        vlay->addWidget(sub);

        auto* btnRow = new QHBoxLayout;
        btnRow->setSpacing(8);

        auto* downloadBtn = new QPushButton("Download", dlg);
        downloadBtn->setStyleSheet(
            "QPushButton { background:#0078d4; color:#fff; border:none; "
            "border-radius:4px; padding:8px 20px; font-size:13px; font-weight:bold; }"
            "QPushButton:hover { background:#1a8de4; }");
        connect(downloadBtn, &QPushButton::clicked, dlg, [url, dlg]() {
            QDesktopServices::openUrl(QUrl(url));
            dlg->accept();
        });

        auto* laterBtn = new QPushButton("👍 No, Thanks", dlg);
        laterBtn->setStyleSheet(
            "QPushButton { background:#2a2a2a; color:#aaa; border:1px solid #444; "
            "border-radius:4px; padding:8px 20px; font-size:13px; }"
            "QPushButton:hover { background:#333; color:#fff; }");
        connect(laterBtn, &QPushButton::clicked, dlg, [dlg, latestClean]() {
            // Save this version as skipped so it never prompts again
            QSettings settings("HijackAssassin", "MediaCountdowns");
            QStringList skipped = settings.value("skippedUpdates").toStringList();
            if (!skipped.contains(latestClean)) {
                skipped << latestClean;
                settings.setValue("skippedUpdates", skipped);
            }
            dlg->reject();
        });

        btnRow->addWidget(downloadBtn);
        btnRow->addWidget(laterBtn);
        btnRow->addStretch();
        vlay->addLayout(btnRow);

        dlg->exec();
    });
}
