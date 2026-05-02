#pragma once
#include <QMainWindow>
#include <QTabWidget>
#include <QScrollArea>
#include <QWidget>
#include <QGridLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QListWidget>
#include <QTimer>
#include <QList>
#include <QVariantAnimation>
#include <QDialog>
#include <QPlainTextEdit>
#include <QShortcut>
#include <QSystemTrayIcon>
#include "tiledata.h"
#include "tmdbscraper.h"

class TileWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void onSearchClicked();
    void onCustomTileClicked();
    void onSearchResultsReady(const QList<SearchResult>& results);
    void onCreditsReady(int tmdbId, const QString& director, const QString& castLine);
    void onPickerItemActivated(QListWidgetItem* item);
    void onScraperDataReady(const TileData& data);
    void onTileRefreshed(const TileData& updated);
    void onPosterReady(const QString& tileId, const QString& localPath);
    void onScraperError(const QString& msg);
    void onImageChanged(const QString& tileId, const QString& path);
    void onTileDataChanged(const QString& tileId);
    void onRemoveTile(const QString& tileId);
    void onDuplicateTile(const QString& tileId);
    void onRefetchRequested(const QString& tileId);
    void onTestNotification(const QString& tileId);
    void onGlobalTick();
    void onExportClicked();
    void onImportClicked();

private:
    void loadTiles();
    void saveTiles();
    void refreshAllTiles();
    void sortAndRebuildAllTabs();
    void rebuildTab(int tab);
    int  tabForTile(const TileData& td) const;
    void appendTileWidget(const TileData& data);
    void setInputBusy(bool busy);
    void showPicker(const QList<SearchResult>& results);
    void hidePicker();
    void repositionPicker();
    void notifyTrayApp();
    void cleanupOrphanedFiles();
    void installSmoothScroll(QScrollArea* area);
    void smoothScrollBy(QScrollArea* area, int delta);
    void setupDebugWindow();
    void showDebugWindow();
    void fireDirectNotification(const TileData& td);

    static constexpr int COLS        = 3;
    static constexpr int TAB_ACTIVE   = 0;
    static constexpr int TAB_RELEASED = 1;
    static constexpr int TAB_OTHER    = 2;

    QTabWidget*   m_tabs             = nullptr;
    QScrollArea*  m_scrollAreas[3]   = {};
    QWidget*      m_tabContainers[3] = {};
    QGridLayout*  m_grids[3]         = {};

    QWidget*     m_bottomBar       = nullptr;
    QLineEdit*   m_searchEdit      = nullptr;
    QPushButton* m_searchBtn       = nullptr;
    QLabel*      m_statusLbl       = nullptr;

    QWidget*     m_pickerFrame     = nullptr;
    QListWidget* m_pickerList      = nullptr;

    QVariantAnimation* m_scrollAnim[3]   = {};
    int                m_scrollTarget[3] = {};

    QList<SearchResult> m_currentResults;
    QList<TileData>     m_tiles;
    QList<TileWidget*>  m_tileWidgets;
    TmdbScraper*        m_scraper        = nullptr;
    QTimer*             m_globalTick     = nullptr;
    int                 m_refreshPending = 0;

    // ── Debug window ──────────────────────────────────────────────────────────
    QDialog*        m_debugWindow    = nullptr;
    QPlainTextEdit* m_debugLog       = nullptr;

    // ── Direct test notification tray icon ────────────────────────────────────
    // A hidden system tray icon owned by the main app, used only to fire
    // test notifications directly — no IPC to TrayApp required.
    QSystemTrayIcon* m_testTray      = nullptr;
};
