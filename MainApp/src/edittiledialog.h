#pragma once
#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QDateEdit>
#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include "tiledata.h"

class EditTileDialog : public QDialog
{
    Q_OBJECT
public:
    explicit EditTileDialog(const TileData& data, QWidget* parent = nullptr);

    QString selectedImagePath() const { return m_imagePath; }
    bool    imageWasReset()     const { return m_imageReset; }
    bool    titleWasReset()     const { return m_titleReset; }
    bool    dateWasReset()      const { return m_dateReset; }
    bool    timeWasReset()      const { return m_timeReset; }
    bool    anyResetPressed()   const { return m_imageReset || m_titleReset || m_dateReset || m_timeReset; }
    QString customTitle()       const { return m_titleEdit->text().trimmed(); }
    QDate   customDate()        const;   // invalid = clear override / use TMDB
    QString customDateStr()     const;
    QTime   customAirTime()     const;
    bool    wantsRemove()       const { return m_wantsRemove; }

signals:
    // Emitted when the user clicks "Test Notification" inside the dialog.
    // The dialog stays open; TileWidget handles the actual IPC call.
    void testNotificationRequested();

private slots:
    void onSelectImage();
    void onResetTitle();
    void onResetDate();
    void onResetTime();
    void onResetImage();
    void onDateToggled(bool enabled);
    void onSave();
    void onRemove();

private:
    void updatePreview(const QString& path);

    TileData     m_data;
    QString      m_imagePath;
    bool         m_imageReset  = false;
    bool         m_titleReset  = false;
    bool         m_dateReset   = false;
    bool         m_timeReset   = false;
    bool         m_wantsRemove = false;

    QLabel*      m_previewLabel = nullptr;
    QLabel*      m_pathLabel    = nullptr;
    QLineEdit*   m_titleEdit    = nullptr;
    QCheckBox*   m_dateCheck    = nullptr;   // "Set a release date"
    QDateEdit*   m_dateEdit     = nullptr;
    QComboBox*   m_hourCombo    = nullptr;
    QComboBox*   m_minuteCombo  = nullptr;
    QComboBox*   m_ampmCombo    = nullptr;
};
