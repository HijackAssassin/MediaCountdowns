#include "edittiledialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QPixmap>
#include <QFrame>
#include <QMessageBox>
#include <QDateTime>
#include <QSet>
#include <QTimer>

static const char* kField =
    "QLineEdit, QComboBox, QSpinBox { background:#1e1e1e; color:#ffffff; "
    "border:1px solid #3a3a3a; border-radius:4px; padding:8px 10px; font-size:14px; }"
    "QLineEdit:focus, QSpinBox:focus { border-color:#0078d4; }"
    "QComboBox::drop-down { border:none; }"
    "QComboBox QAbstractItemView { background:#1e1e1e; color:#fff; "
    "selection-background-color:#0078d4; }"
    "QSpinBox::up-button, QSpinBox::down-button { width:0; border:none; }";

static const char* kDisabledField =
    "QComboBox, QSpinBox { background:#141414; color:#444; "
    "border:1px solid #222; border-radius:4px; padding:8px 10px; font-size:14px; }";

// Reset button styles: greyed = unchanged, active = something was modified
static const char* kResetGreyed =
    "QPushButton { background:#1a1a1a; color:#444; border:1px solid #252525; "
    "border-radius:4px; padding:8px 14px; font-size:12px; }";
static const char* kResetActive =
    "QPushButton { background:#2a2a2a; color:#aaa; border:1px solid #3a3a3a; "
    "border-radius:4px; padding:8px 14px; font-size:12px; }"
    "QPushButton:hover { background:#383838; color:#fff; }";

static const char* kSection = "color:#888; font-size:11px; margin-top:4px;";

static const char* kMonths[] = {
    "January","February","March","April","May","June",
    "July","August","September","October","November","December"
};

static const char* kPresets[] = {
    "Custom","Birthday","Christmas","Easter","Halloween","Thanksgiving",
    "New Year","April Fools","Good Friday","Veterans Day","Independence Day"
};
static const int kPresetCount = 11;

static const QSet<QString> kHolidayPresets = {
    "Christmas","Easter","Halloween","Thanksgiving","New Year","April Fools",
    "Good Friday","Veterans Day","Independence Day","Birthday"
};

static QDate easterDate(int y) {
    int a=y%19,b=y/100,c=y%100,d=b/4,e=b%4,f=(b+8)/25;
    int g=(b-f+1)/3,h=(19*a+b-d-g+15)%30,i=c/4,k=c%4;
    int l=(32+2*e+2*i-h-k)%7,m=(a+11*h+22*l)/451;
    return QDate(y,(h+l-7*m+114)/31,((h+l-7*m+114)%31)+1);
}
static QDate goodFridayDate(int y) { return easterDate(y).addDays(-2); }

static QDate nextOccurrence(const QString& preset, const QDate& ref) {
    int y = ref.year();
    auto tryBoth = [&](int m, int day) -> QDate {
        QDate a(y,m,day); return (a>=ref)?a:QDate(y+1,m,day);
    };
    if      (preset=="Christmas")        return tryBoth(12,25);
    else if (preset=="Halloween")        return tryBoth(10,31);
    else if (preset=="Thanksgiving") {
        auto thanksgiving = [](int y) -> QDate {
            QDate d(y, 11, 1);
            int dow = d.dayOfWeek();
            int firstThurs = 1 + ((4 - dow + 7) % 7);
            return QDate(y, 11, firstThurs + 21);
        };
        QDate d = thanksgiving(y); return (d >= ref) ? d : thanksgiving(y+1);
    }
    else if (preset=="New Year")         return tryBoth(1,1);
    else if (preset=="April Fools")      return tryBoth(4,1);
    else if (preset=="Veterans Day")     return tryBoth(11,11);
    else if (preset=="Independence Day") return tryBoth(7,4);
    else if (preset=="Easter")  { QDate d=easterDate(y);     return (d>=ref)?d:easterDate(y+1); }
    else if (preset=="Good Friday") { QDate d=goodFridayDate(y); return (d>=ref)?d:goodFridayDate(y+1); }
    return QDate();
}

EditTileDialog::EditTileDialog(const TileData& data, QWidget* parent)
    : QDialog(parent), m_data(data), m_imagePath(data.imagePath)
{
    setWindowTitle("Edit Tile");
    setMinimumWidth(540);
    setMaximumWidth(540);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    setModal(true);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    // Store initial values for dirty-checking reset buttons.
    // m_initDate is always the TMDB/original targetDate — so the reset button
    // stays active whenever the user has overridden it with a custom date.
    m_initTitle     = data.customTitle.isEmpty() ? data.title : data.customTitle;
    m_initDate      = data.targetDate;
    m_initImagePath = data.imagePath;

    auto* vlay = new QVBoxLayout(this);
    vlay->setContentsMargins(20, 16, 20, 16);
    vlay->setSpacing(4);

    auto addLabel = [&](const QString& t) -> QLabel* {
        auto* l = new QLabel(t, this);
        l->setStyleSheet("color:#888; font-size:11px; margin-top:6px;");
        vlay->addWidget(l); return l;
    };

    // ── Preset ────────────────────────────────────────────────────────────────
    addLabel("Preset");
    m_presetCombo = new QComboBox(this);
    m_presetCombo->setStyleSheet(kField);
    // "Media" only appears for tiles added via search
    if (data.presetType == "Media") m_presetCombo->addItem("Media");
    for (int i = 0; i < kPresetCount; ++i) m_presetCombo->addItem(kPresets[i]);
    {
        int idx = m_presetCombo->findText(data.presetType);
        m_presetCombo->setCurrentIndex(idx < 0 ? m_presetCombo->findText("Custom") : idx);
    }
    connect(m_presetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &EditTileDialog::onPresetChanged);
    vlay->addWidget(m_presetCombo);

    // ── Display Name ──────────────────────────────────────────────────────────
    addLabel("Display Name");
    auto* titleRow = new QHBoxLayout; titleRow->setSpacing(6);
    m_titleEdit = new QLineEdit(this);
    m_titleEdit->setStyleSheet(kField);
    m_titleEdit->setText(m_initTitle);
    m_titleEdit->setPlaceholderText(data.title);
    titleRow->addWidget(m_titleEdit, 1);
    m_rTitleBtn = new QPushButton("Reset", this);
    m_rTitleBtn->setFixedWidth(64);
    connect(m_rTitleBtn, &QPushButton::clicked, this, &EditTileDialog::onResetTitle);
    titleRow->addWidget(m_rTitleBtn);
    vlay->addLayout(titleRow);

    // ── Date ──────────────────────────────────────────────────────────────────
    m_dateLabel = addLabel("Date");
    bool hasDate = data.hasDate();  // respects noDateOverride
    m_dateCheckState = hasDate;
    m_dateCheck = new QCheckBox("Has a date", this);
    m_dateCheck->setStyleSheet("color:#cccccc; font-size:13px;");
    m_dateCheck->setChecked(hasDate);
    connect(m_dateCheck, &QCheckBox::toggled, this, &EditTileDialog::onDateToggled);
    vlay->addWidget(m_dateCheck);

    QDate showDate = data.customDate.isValid() ? data.customDate
                   : data.targetDate.isValid() ? data.targetDate
                   : QDate::currentDate();

    m_dateRowWidget = new QWidget(this);
    m_dateRowWidget->setAttribute(Qt::WA_TranslucentBackground);
    auto* dateRow = new QHBoxLayout(m_dateRowWidget);
    dateRow->setContentsMargins(0,0,0,0); dateRow->setSpacing(6);

    m_monthCombo = new QComboBox(this);
    m_monthCombo->setStyleSheet(kField);
    for (int i = 0; i < 12; ++i) m_monthCombo->addItem(kMonths[i]);
    m_monthCombo->setCurrentIndex(showDate.month() - 1);
    m_monthCombo->setEnabled(hasDate);
    connect(m_monthCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &EditTileDialog::onMonthChanged);
    dateRow->addWidget(m_monthCombo, 2);

    m_dayCombo = new QComboBox(this);
    m_dayCombo->setStyleSheet(kField);
    m_dayCombo->setFixedWidth(80);
    m_dayCombo->setEnabled(hasDate);
    refreshDayCombo();
    m_dayCombo->setCurrentIndex(showDate.day() - 1);
    dateRow->addWidget(m_dayCombo, 1);

    m_yearSpin = new QSpinBox(this);
    m_yearSpin->setStyleSheet(kField);
    m_yearSpin->setRange(2000, 2099);
    m_yearSpin->setValue(showDate.year());
    m_yearSpin->setFixedWidth(90);
    m_yearSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_yearSpin->setEnabled(hasDate);
    connect(m_yearSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int){ refreshDayCombo(); updateResetButtons(); });
    dateRow->addWidget(m_yearSpin, 1);

    m_rDateBtn = new QPushButton("Reset", this);
    m_rDateBtn->setFixedWidth(64);
    connect(m_rDateBtn, &QPushButton::clicked, this, &EditTileDialog::onResetDate);
    dateRow->addWidget(m_rDateBtn);
    vlay->addWidget(m_dateRowWidget);

    // ── Time ─────────────────────────────────────────────────────────────────
    m_timeLabel = addLabel("Time  (optional — defaults to midnight)");
    m_timeRowWidget = new QWidget(this);
    m_timeRowWidget->setAttribute(Qt::WA_TranslucentBackground);
    auto* timeRow = new QHBoxLayout(m_timeRowWidget);
    timeRow->setContentsMargins(0,0,0,0); timeRow->setSpacing(6);

    m_hourCombo = new QComboBox(this); m_hourCombo->setStyleSheet(kField); m_hourCombo->setFixedWidth(70);
    for (int h = 1; h <= 12; ++h) m_hourCombo->addItem(QString::number(h));
    m_minuteCombo = new QComboBox(this); m_minuteCombo->setStyleSheet(kField); m_minuteCombo->setFixedWidth(68);
    for (int m = 0; m < 60; ++m) m_minuteCombo->addItem(QString("%1").arg(m, 2, 10, QChar('0')));
    m_ampmCombo = new QComboBox(this); m_ampmCombo->setStyleSheet(kField); m_ampmCombo->setFixedWidth(68);
    m_ampmCombo->addItem("AM"); m_ampmCombo->addItem("PM");

    // m_initTime = TMDB/original airTime (for reset comparison)
    // Display the currently-saved customAirTime in the combos if one exists
    m_initTime  = data.airTime.isValid() ? data.airTime : QTime(0, 0);
    QTime showTime = data.customAirTime.isValid() ? data.customAirTime : m_initTime;

    int h12 = showTime.hour() % 12; if (h12 == 0) h12 = 12;
    m_hourCombo->setCurrentIndex(h12 - 1);
    m_minuteCombo->setCurrentIndex(showTime.minute());
    m_ampmCombo->setCurrentIndex(showTime.hour() >= 12 ? 1 : 0);

    timeRow->addWidget(new QLabel("Hour:", this)); timeRow->addWidget(m_hourCombo);
    timeRow->addWidget(new QLabel(":", this));      timeRow->addWidget(m_minuteCombo);
    timeRow->addWidget(m_ampmCombo); timeRow->addStretch();
    m_rTimeBtn = new QPushButton("Reset", this);
    m_rTimeBtn->setFixedWidth(64);
    m_rTimeBtn->setToolTip("Reset to midnight");
    connect(m_rTimeBtn, &QPushButton::clicked, this, &EditTileDialog::onResetTime);
    timeRow->addWidget(m_rTimeBtn);
    vlay->addWidget(m_timeRowWidget);

    // ── Weekday row (Weekly only) ─────────────────────────────────────────────
    m_weekdayLabel = addLabel("Weekday");
    m_weekdayRowWidget = new QWidget(this);
    m_weekdayRowWidget->setAttribute(Qt::WA_TranslucentBackground);
    auto* wdRow = new QHBoxLayout(m_weekdayRowWidget);
    wdRow->setContentsMargins(0,0,0,0); wdRow->setSpacing(6);
    m_weekdayCombo = new QComboBox(this); m_weekdayCombo->setStyleSheet(kField);
    const char* days[] = {"Monday","Tuesday","Wednesday","Thursday","Friday","Saturday","Sunday"};
    for (const char* d : days) m_weekdayCombo->addItem(d);
    m_weekdayCombo->setCurrentIndex(qMax(0, data.loopWeekday - 1));
    wdRow->addWidget(m_weekdayCombo); wdRow->addStretch();
    vlay->addWidget(m_weekdayRowWidget);

    // ── Day of month row (Monthly only) ──────────────────────────────────────
    m_domLabel = addLabel("Day of Month");
    m_domRowWidget = new QWidget(this);
    m_domRowWidget->setAttribute(Qt::WA_TranslucentBackground);
    auto* domRow = new QHBoxLayout(m_domRowWidget);
    domRow->setContentsMargins(0,0,0,0); domRow->setSpacing(6);
    m_domSpin = new QSpinBox(this); m_domSpin->setStyleSheet(kField);
    m_domSpin->setRange(1, 31); m_domSpin->setValue(qMax(1, data.loopDayOfMonth));
    m_domSpin->setFixedWidth(90); m_domSpin->setButtonSymbols(QAbstractSpinBox::UpDownArrows);
    auto* domSufLbl = new QLabel("of each month", this);
    domSufLbl->setStyleSheet("color:#aaa; font-size:13px;");
    domRow->addWidget(m_domSpin); domRow->addWidget(domSufLbl); domRow->addStretch();
    vlay->addWidget(m_domRowWidget);

    // ── Loop controls ─────────────────────────────────────────────────────────
    auto* sepLoop = new QFrame(this);
    sepLoop->setFrameShape(QFrame::HLine); sepLoop->setStyleSheet("color:#2a2a2a;");
    vlay->addWidget(sepLoop);

    auto* loopRow = new QHBoxLayout; loopRow->setSpacing(10);
    m_loopCheck = new QCheckBox("Loop", this);
    m_loopCheck->setStyleSheet("color:#cccccc; font-size:13px; font-weight:bold;");
    m_loopCheck->setChecked(data.isLooped);
    connect(m_loopCheck, &QCheckBox::toggled, this, &EditTileDialog::onLoopToggled);
    loopRow->addWidget(m_loopCheck);

    m_loopIntervalCombo = new QComboBox(this);
    m_loopIntervalCombo->addItem("Yearly");
    m_loopIntervalCombo->addItem("Monthly");
    m_loopIntervalCombo->addItem("Weekly");
    m_loopIntervalCombo->addItem("Daily");
    {
        int idx = m_loopIntervalCombo->findText(data.loopInterval);
        m_loopIntervalCombo->setCurrentIndex(idx < 0 ? 0 : idx);
    }
    bool loopEnabled = data.isLooped;
    m_loopIntervalCombo->setEnabled(loopEnabled);
    m_loopIntervalCombo->setStyleSheet(loopEnabled ? kField : kDisabledField);
    // Hide interval picker for holiday presets (always yearly)
    m_loopIntervalCombo->setVisible(!kHolidayPresets.contains(data.presetType));
    connect(m_loopIntervalCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &EditTileDialog::onLoopIntervalChanged);
    loopRow->addWidget(m_loopIntervalCombo, 1);
    loopRow->addStretch();
    vlay->addLayout(loopRow);

    // ── Divider ───────────────────────────────────────────────────────────────
    auto* sep = new QFrame(this);
    sep->setFrameShape(QFrame::HLine); sep->setStyleSheet("color:#2a2a2a;");
    vlay->addWidget(sep);

    // ── Backdrop Image ────────────────────────────────────────────────────────
    addLabel("Backdrop Image");
    m_previewLabel = new QLabel(this);
    m_previewLabel->setFixedSize(500, 281);
    m_previewLabel->setAlignment(Qt::AlignCenter);
    m_previewLabel->setStyleSheet(
        "QLabel { background:#000; border:1px solid #3a3a3a; border-radius:4px; }");
    updatePreview(m_imagePath);
    vlay->addWidget(m_previewLabel);
    m_pathLabel = new QLabel(m_imagePath.isEmpty() ? "Auto-fetched from TMDB" : m_imagePath, this);
    m_pathLabel->setStyleSheet("color:#555; font-size:10px;");
    vlay->addWidget(m_pathLabel);

    auto* imgRow = new QHBoxLayout; imgRow->setSpacing(8);
    auto* selBtn = new QPushButton("Select Image…", this);
    selBtn->setStyleSheet(
        "QPushButton { background:#2a2a2a; color:#ccc; border:1px solid #444; "
        "border-radius:4px; padding:8px 16px; font-size:13px; }"
        "QPushButton:hover { background:#383838; }");
    connect(selBtn, &QPushButton::clicked, this, &EditTileDialog::onSelectImage);
    imgRow->addWidget(selBtn);
    m_rImageBtn = new QPushButton("Reset Image", this);
    connect(m_rImageBtn, &QPushButton::clicked, this, &EditTileDialog::onResetImage);
    imgRow->addWidget(m_rImageBtn);
    imgRow->addStretch();
    vlay->addLayout(imgRow);

    // ── Bottom buttons ────────────────────────────────────────────────────────
    auto* sep2 = new QFrame(this);
    sep2->setFrameShape(QFrame::HLine); sep2->setStyleSheet("color:#2a2a2a;");
    vlay->addWidget(sep2);
    auto* btnRow = new QHBoxLayout; btnRow->setSpacing(8);
    auto* removeBtn = new QPushButton("Remove Tile", this);
    removeBtn->setStyleSheet(
        "QPushButton { background:#4a1515; color:#ff8888; border:1px solid #662222; "
        "border-radius:4px; padding:8px 16px; font-size:13px; }"
        "QPushButton:hover { background:#661818; }");
    connect(removeBtn, &QPushButton::clicked, this, &EditTileDialog::onRemove);
    btnRow->addWidget(removeBtn);

    auto* testNotifBtn = new QPushButton("🔔  Test Notification", this);
    testNotifBtn->setStyleSheet(
        "QPushButton { background:#1a2a1a; color:#88dd88; border:1px solid #336633; "
        "border-radius:4px; padding:8px 16px; font-size:13px; }"
        "QPushButton:hover { background:#223322; }");
    connect(testNotifBtn, &QPushButton::clicked,
            this, [this](){ emit testNotificationRequested(); });
    btnRow->addWidget(testNotifBtn);

    btnRow->addStretch();
    auto* cancelBtn = new QPushButton("Cancel", this);
    cancelBtn->setStyleSheet(
        "QPushButton { background:#252525; color:#aaa; border:1px solid #444; "
        "border-radius:4px; padding:8px 20px; font-size:13px; }"
        "QPushButton:hover { background:#333; }");
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    btnRow->addWidget(cancelBtn);
    auto* saveBtn = new QPushButton("Save", this);
    saveBtn->setStyleSheet(
        "QPushButton { background:#0078d4; color:#fff; border:none; "
        "border-radius:4px; padding:8px 28px; font-size:13px; font-weight:bold; }"
        "QPushButton:hover { background:#1a8de4; }");
    connect(saveBtn, &QPushButton::clicked, this, &EditTileDialog::onSave);
    btnRow->addWidget(saveBtn);
    vlay->addLayout(btnRow);

    // Connect change signals for reset button dirty-checking
    connect(m_titleEdit, &QLineEdit::textChanged,
            this, &EditTileDialog::updateResetButtons);
    connect(m_monthCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &EditTileDialog::updateResetButtons);
    connect(m_dayCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &EditTileDialog::updateResetButtons);
    connect(m_hourCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &EditTileDialog::updateResetButtons);
    connect(m_minuteCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &EditTileDialog::updateResetButtons);
    connect(m_ampmCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &EditTileDialog::updateResetButtons);

    applyLoopFieldVisibility();
    updateResetButtons();
}

// =============================================================================
QString EditTileDialog::presetType() const
{
    return m_presetCombo->currentText();
}

// =============================================================================
void EditTileDialog::updateResetButtons()
{
    // Title dirty: current text differs from TMDB original title
    bool titleDirty = (m_titleEdit->text().trimmed() != m_data.title);
    m_rTitleBtn->setStyleSheet(titleDirty ? kResetActive : kResetGreyed);
    m_rTitleBtn->setEnabled(titleDirty);

    // Date dirty: UI date fields differ from TMDB targetDate
    bool dateDirty = m_dateRowWidget->isVisible() && (selectedDate() != m_initDate);
    m_rDateBtn->setStyleSheet(dateDirty ? kResetActive : kResetGreyed);
    m_rDateBtn->setEnabled(dateDirty);

    // Time dirty: UI time combos differ from TMDB/original airTime
    int h12  = m_hourCombo->currentIndex() + 1;
    int mins = m_minuteCombo->currentIndex();
    bool pm  = (m_ampmCombo->currentIndex() == 1);
    int h24  = (h12 % 12) + (pm ? 12 : 0);
    QTime curTime(h24, mins);
    bool timeDirty = (curTime != m_initTime);
    m_rTimeBtn->setStyleSheet(timeDirty ? kResetActive : kResetGreyed);
    m_rTimeBtn->setEnabled(timeDirty);

    // Image dirty: path differs from original
    bool imgDirty = (m_imagePath != m_initImagePath);
    m_rImageBtn->setStyleSheet(imgDirty ? kResetActive : kResetGreyed);
    m_rImageBtn->setEnabled(imgDirty);
}

// =============================================================================
void EditTileDialog::applyLoopFieldVisibility()
{
    bool looped  = m_loopCheck->isChecked();
    QString intv = looped ? m_loopIntervalCombo->currentText() : "Yearly";
    QString preset = m_presetCombo->currentText();
    bool isHoliday = kHolidayPresets.contains(preset);

    // If "has a date" checkbox is unchecked, hide everything below it
    bool dateEnabled = isHoliday || m_dateCheckState;

    bool showDate    = dateEnabled && (!looped || intv == "Yearly");
    bool showWeekday = dateEnabled && looped && intv == "Weekly";
    bool showDOM     = dateEnabled && looped && intv == "Monthly";

    // "Has a date" checkbox hidden for holiday presets (always have a date)
    m_dateCheck->setVisible(!isHoliday && (!looped || intv == "Yearly"));
    m_dateLabel->setVisible(showDate);
    m_dateRowWidget->setVisible(showDate);

    m_timeLabel->setText(
        (looped && intv != "Yearly") ? "Time of day"
                                     : "Time  (optional — defaults to midnight)");
    m_timeLabel->setVisible(dateEnabled);
    m_timeRowWidget->setVisible(dateEnabled);

    m_weekdayLabel->setVisible(showWeekday);
    m_weekdayRowWidget->setVisible(showWeekday);

    m_domLabel->setVisible(showDOM);
    m_domRowWidget->setVisible(showDOM);

    m_loopCheck->setVisible(dateEnabled);
    m_loopIntervalCombo->setVisible(dateEnabled && !isHoliday);

    // Shrink/grow dialog to fit visible content — eliminates empty gaps
    QTimer::singleShot(0, this, [this]{ adjustSize(); });
}

// =============================================================================
// Helpers
// =============================================================================
int EditTileDialog::daysInSelectedMonth() const
{
    int month = m_monthCombo->currentIndex() + 1;
    int year  = m_yearSpin ? m_yearSpin->value() : QDate::currentDate().year();
    return QDate(year, month, 1).daysInMonth();
}

void EditTileDialog::refreshDayCombo()
{
    int prevDay = m_dayCombo->currentIndex() + 1;
    int days    = daysInSelectedMonth();
    m_dayCombo->blockSignals(true);
    m_dayCombo->clear();
    for (int d = 1; d <= days; ++d) m_dayCombo->addItem(QString::number(d));
    m_dayCombo->setCurrentIndex(qMin(prevDay, days) - 1);
    m_dayCombo->blockSignals(false);
}

void EditTileDialog::onMonthChanged(int) { refreshDayCombo(); updateResetButtons(); }

QDate EditTileDialog::selectedDate() const
{
    return QDate(m_yearSpin->value(),
                 m_monthCombo->currentIndex() + 1,
                 m_dayCombo->currentIndex() + 1);
}

void EditTileDialog::setSelectedDate(const QDate& d)
{
    if (!d.isValid()) return;
    m_monthCombo->setCurrentIndex(d.month() - 1);
    refreshDayCombo();
    m_dayCombo->setCurrentIndex(d.day() - 1);
    m_yearSpin->setValue(d.year());
}

QDate EditTileDialog::customDate() const
{
    bool looped  = m_loopCheck->isChecked();
    QString intv = m_loopIntervalCombo->currentText();

    if (looped && intv == "Weekly") {
        int wd = m_weekdayCombo->currentIndex() + 1;
        QDate today = QDate::currentDate();
        int daysAhead = (wd - today.dayOfWeek() + 7) % 7;
        if (daysAhead == 0) daysAhead = 7;
        return today.addDays(daysAhead);
    }
    if (looped && intv == "Monthly") {
        int dom = m_domSpin->value();
        QDate today = QDate::currentDate();
        QDate d = QDate(today.year(), today.month(), qMin(dom, today.daysInMonth()));
        if (d <= today) d = d.addMonths(1);
        return QDate(d.year(), d.month(), qMin(dom, d.daysInMonth()));
    }
    if (looped && intv == "Daily") {
        int h12 = m_hourCombo->currentIndex() + 1;
        int mins = m_minuteCombo->currentIndex();
        bool pm  = (m_ampmCombo->currentIndex() == 1);
        int h24  = (h12 % 12) + (pm ? 12 : 0);
        QTime t(h24, mins);
        QDateTime now  = QDateTime::currentDateTime();
        QDateTime next(now.date(), t);
        if (next <= now) next = next.addDays(1);
        return next.date();
    }
    if (!m_dateCheck->isChecked()) return QDate();
    QDate chosen = selectedDate();
    return (chosen == m_data.targetDate && !m_data.customDate.isValid()) ? QDate() : chosen;
}

QString EditTileDialog::customDateStr() const
{
    QDate d = customDate();
    return d.isValid() ? d.toString("MMMM d, yyyy") : QString();
}

QTime EditTileDialog::customAirTime() const
{
    int h12  = m_hourCombo->currentIndex() + 1;
    int mins = m_minuteCombo->currentIndex();
    bool pm  = (m_ampmCombo->currentIndex() == 1);
    int h24  = (h12 % 12) + (pm ? 12 : 0);
    QTime t(h24, mins);
    // Only return invalid (clear the custom time) when the user explicitly
    // hit Reset Time and the result is midnight — not just because the dial
    // happens to show 12:00 AM which could be a legitimate saved value.
    if (m_timeReset && t == QTime(0, 0)) return QTime();
    return t;
}

// =============================================================================
// Slots
// =============================================================================
void EditTileDialog::onPresetChanged(int index)
{
    QString preset = m_presetCombo->itemText(index);
    bool isHoliday = kHolidayPresets.contains(preset);

    // Update title to preset name, or restore original if switching to Custom
    if (preset == "Custom" || preset == "Media") {
        m_titleEdit->setText(m_initTitle);
        m_loopIntervalCombo->setVisible(true);
        // Restore date to original saved date (or Jan 1 if none)
        QDate restoreDate = m_initDate.isValid() ? m_initDate
                          : QDate(QDate::currentDate().year(), 1, 1);
        setSelectedDate(restoreDate);
        m_dateCheckState = m_initDate.isValid();
        m_dateCheck->setChecked(m_dateCheckState);
    } else {
        m_titleEdit->setText(preset);
        // Holiday presets: enable loop, set yearly, hide interval picker
        m_loopCheck->setChecked(true);
        m_loopIntervalCombo->setEnabled(true);
        m_loopIntervalCombo->setStyleSheet(kField);
        m_loopIntervalCombo->setCurrentIndex(0); // Yearly
        m_loopIntervalCombo->setVisible(!isHoliday);

        // Birthday: default Jan 1 current year
        if (preset == "Birthday") {
            setSelectedDate(QDate(QDate::currentDate().year(), 1, 1));
        } else {
            QDate d = nextOccurrence(preset, QDate::currentDate());
            if (d.isValid()) setSelectedDate(d);
        }
    }
    applyLoopFieldVisibility();
    updateResetButtons();
}

void EditTileDialog::onLoopToggled(bool checked)
{
    m_loopIntervalCombo->setEnabled(checked);
    m_loopIntervalCombo->setStyleSheet(checked ? kField : kDisabledField);
    applyLoopFieldVisibility();
}

void EditTileDialog::onLoopIntervalChanged(int) { applyLoopFieldVisibility(); }

void EditTileDialog::onDateToggled(bool enabled)
{
    m_dateCheckState = enabled;
    m_monthCombo->setEnabled(enabled);
    m_dayCombo->setEnabled(enabled);
    m_yearSpin->setEnabled(enabled);
    QString s = enabled ? kField : kDisabledField;
    m_monthCombo->setStyleSheet(s);
    m_dayCombo->setStyleSheet(s);
    m_yearSpin->setStyleSheet(s);
    applyLoopFieldVisibility();
}

void EditTileDialog::onResetTitle()
{
    m_titleReset = true;
    m_titleEdit->setText(m_data.title);
    // updateResetButtons called via textChanged signal
}

void EditTileDialog::onResetDate()
{
    m_dateReset = true;
    if (m_data.targetDate.isValid()) {
        m_dateCheckState = true;
        m_dateCheck->setChecked(true);
        setSelectedDate(m_data.targetDate);
    } else {
        m_dateCheckState = false;
        m_dateCheck->setChecked(false);
    }
    applyLoopFieldVisibility();
    updateResetButtons();
}

void EditTileDialog::onResetTime()
{
    m_timeReset = true;
    int h12 = m_initTime.hour() % 12; if (h12 == 0) h12 = 12;
    m_hourCombo->setCurrentIndex(h12 - 1);
    m_minuteCombo->setCurrentIndex(m_initTime.minute());
    m_ampmCombo->setCurrentIndex(m_initTime.hour() >= 12 ? 1 : 0);
    // updateResetButtons called via combo signals
}

void EditTileDialog::onResetImage()
{
    m_imageReset = true;
    m_imagePath  = "";
    m_pathLabel->setText("Will re-download TMDB backdrop on save");
    m_previewLabel->clear();
    m_previewLabel->setStyleSheet(
        "QLabel { background:#111; border:1px solid #3a3a3a; border-radius:4px; }");
    updateResetButtons();
}

void EditTileDialog::onSelectImage()
{
    QString path = QFileDialog::getOpenFileName(
        this, "Select Backdrop Image", QString(),
        "Images (*.png *.jpg *.jpeg *.bmp *.webp)");
    if (path.isEmpty()) return;
    QString ext  = QFileInfo(path).suffix().toLower();
    QString dir  = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                   + "/custom_images";
    QDir().mkpath(dir);
    QString dest = dir + "/" + m_data.id + "_custom." + ext;
    if (QFile::exists(dest)) QFile::remove(dest);
    m_imagePath  = QFile::copy(path, dest) ? dest : path;
    m_imageReset = false;
    updatePreview(m_imagePath);
    m_pathLabel->setText(m_imagePath);
    updateResetButtons();
}

void EditTileDialog::onSave()  { accept(); }

void EditTileDialog::onRemove()
{
    auto btn = QMessageBox::question(this, "Remove Tile",
        QString("Remove \"%1\"?").arg(m_data.displayTitle()),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (btn == QMessageBox::Yes) { m_wantsRemove = true; accept(); }
}

void EditTileDialog::updatePreview(const QString& path)
{
    if (path.isEmpty()) { m_previewLabel->clear(); return; }
    QPixmap px(path);
    if (px.isNull()) return;
    m_previewLabel->setPixmap(
        px.scaled(500, 281, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
}
