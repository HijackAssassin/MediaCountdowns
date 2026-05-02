#pragma once
#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QSpinBox>
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
    QDate   customDate()        const;
    QString customDateStr()     const;
    QTime   customAirTime()     const;
    bool    dateChecked()       const { return m_dateCheckState; }
    bool    wantsRemove()       const { return m_wantsRemove; }
    bool    isLooped()          const { return m_loopCheck->isChecked(); }
    QString loopInterval()      const { return m_loopIntervalCombo->currentText(); }
    int     loopWeekday()       const { return m_weekdayCombo->currentIndex() + 1; }
    int     loopDayOfMonth()    const { return m_domSpin->value(); }
    QString presetType()        const;

signals:
    void testNotificationRequested();

private slots:
    void onSelectImage();
    void onResetTitle();
    void onResetDate();
    void onResetTime();
    void onResetImage();
    void onDateToggled(bool enabled);
    void onMonthChanged(int index);
    void onPresetChanged(int index);
    void onLoopToggled(bool checked);
    void onLoopIntervalChanged(int index);
    void onSave();
    void onRemove();

private:
    void updatePreview(const QString& path);
    QDate selectedDate() const;
    void  setSelectedDate(const QDate& d);
    int   daysInSelectedMonth() const;
    void  refreshDayCombo();
    void  applyLoopFieldVisibility();
    void  updateResetButtons();

    TileData     m_data;
    QString      m_imagePath;
    bool         m_imageReset  = false;
    bool         m_titleReset  = false;
    bool         m_dateReset   = false;
    bool         m_timeReset   = false;
    bool         m_wantsRemove = false;
    bool         m_dateCheckState = true;  // tracks checkbox regardless of visibility

    // Initial values for dirty-checking reset buttons
    QString      m_initTitle;
    QDate        m_initDate;
    QTime        m_initTime;
    QString      m_initImagePath;

    QLabel*      m_previewLabel      = nullptr;
    QLabel*      m_pathLabel         = nullptr;
    QComboBox*   m_presetCombo       = nullptr;
    QLineEdit*   m_titleEdit         = nullptr;
    QPushButton* m_rTitleBtn         = nullptr;
    QLabel*      m_dateLabel         = nullptr;
    QCheckBox*   m_dateCheck         = nullptr;
    QWidget*     m_dateRowWidget     = nullptr;
    QComboBox*   m_monthCombo        = nullptr;
    QComboBox*   m_dayCombo          = nullptr;
    QSpinBox*    m_yearSpin          = nullptr;
    QPushButton* m_rDateBtn          = nullptr;
    QLabel*      m_timeLabel         = nullptr;
    QWidget*     m_timeRowWidget     = nullptr;
    QComboBox*   m_hourCombo         = nullptr;
    QComboBox*   m_minuteCombo       = nullptr;
    QComboBox*   m_ampmCombo         = nullptr;
    QPushButton* m_rTimeBtn          = nullptr;
    QLabel*      m_weekdayLabel      = nullptr;
    QWidget*     m_weekdayRowWidget  = nullptr;
    QComboBox*   m_weekdayCombo      = nullptr;
    QLabel*      m_domLabel          = nullptr;
    QWidget*     m_domRowWidget      = nullptr;
    QSpinBox*    m_domSpin           = nullptr;
    QCheckBox*   m_loopCheck         = nullptr;
    QComboBox*   m_loopIntervalCombo = nullptr;
    QPushButton* m_rImageBtn         = nullptr;
};
