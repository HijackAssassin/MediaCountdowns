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

static const char* kField =
    "QLineEdit, QDateEdit, QComboBox { background:#1e1e1e; color:#ffffff; "
    "border:1px solid #3a3a3a; border-radius:4px; padding:8px 10px; font-size:14px; }"
    "QLineEdit:focus, QDateEdit:focus { border-color:#0078d4; }"
    "QComboBox::drop-down { border:none; }"
    "QComboBox QAbstractItemView { background:#1e1e1e; color:#fff; "
    "selection-background-color:#0078d4; }";

static const char* kDisabledField =
    "QDateEdit, QComboBox { background:#141414; color:#444; "
    "border:1px solid #222; border-radius:4px; padding:8px 10px; font-size:14px; }";

static const char* kReset =
    "QPushButton { background:#2a2a2a; color:#aaa; border:1px solid #3a3a3a; "
    "border-radius:4px; padding:8px 14px; font-size:12px; }"
    "QPushButton:hover { background:#383838; color:#fff; }";

static const char* kSection = "color:#888; font-size:11px; margin-top:4px;";

EditTileDialog::EditTileDialog(const TileData& data, QWidget* parent)
    : QDialog(parent), m_data(data), m_imagePath(data.imagePath)
{
    setWindowTitle("Edit Tile");
    setFixedWidth(540);
    setModal(true);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    auto* vlay = new QVBoxLayout(this);
    vlay->setContentsMargins(20, 20, 20, 20);
    vlay->setSpacing(8);

    auto addLabel = [&](const QString& t) {
        auto* l = new QLabel(t, this); l->setStyleSheet(kSection); vlay->addWidget(l);
    };

    // ── Display Name ──────────────────────────────────────────────────────────
    addLabel("Display Name");
    auto* titleRow = new QHBoxLayout; titleRow->setSpacing(6);
    m_titleEdit = new QLineEdit(this);
    m_titleEdit->setStyleSheet(kField);
    m_titleEdit->setText(data.customTitle.isEmpty() ? data.title : data.customTitle);
    m_titleEdit->setPlaceholderText(data.title);
    titleRow->addWidget(m_titleEdit, 1);
    auto* rTitleBtn = new QPushButton("Reset", this);
    rTitleBtn->setStyleSheet(kReset); rTitleBtn->setFixedWidth(64);
    connect(rTitleBtn, &QPushButton::clicked, this, &EditTileDialog::onResetTitle);
    titleRow->addWidget(rTitleBtn);
    vlay->addLayout(titleRow);

    // ── Release Date ──────────────────────────────────────────────────────────
    addLabel("Release Date");

    // Checkbox: does this tile have a date at all?
    bool hasDate = data.effectiveDate().isValid();
    m_dateCheck = new QCheckBox("Has a release date", this);
    m_dateCheck->setStyleSheet("color:#cccccc; font-size:13px;");
    m_dateCheck->setChecked(hasDate);
    connect(m_dateCheck, &QCheckBox::toggled, this, &EditTileDialog::onDateToggled);
    vlay->addWidget(m_dateCheck);

    auto* dateRow = new QHBoxLayout; dateRow->setSpacing(6);
    m_dateEdit = new QDateEdit(this);
    m_dateEdit->setCalendarPopup(true);
    m_dateEdit->setDisplayFormat("MMMM d, yyyy");
    m_dateEdit->setMinimumDate(QDate(2000,1,1));
    m_dateEdit->setMaximumDate(QDate(2099,12,31));
    // Populate: customDate > targetDate > today
    QDate showDate = data.customDate.isValid()  ? data.customDate
                   : data.targetDate.isValid()  ? data.targetDate
                   : QDate::currentDate();
    m_dateEdit->setDate(showDate);
    m_dateEdit->setEnabled(hasDate);
    m_dateEdit->setStyleSheet(hasDate ? kField : kDisabledField);
    dateRow->addWidget(m_dateEdit, 1);

    auto* rDateBtn = new QPushButton("Reset", this);
    rDateBtn->setStyleSheet(kReset); rDateBtn->setFixedWidth(64);
    connect(rDateBtn, &QPushButton::clicked, this, &EditTileDialog::onResetDate);
    dateRow->addWidget(rDateBtn);
    vlay->addLayout(dateRow);

    // ── Release Time ──────────────────────────────────────────────────────────
    addLabel("Release Time");
    auto* timeRow = new QHBoxLayout; timeRow->setSpacing(6);
    m_hourCombo = new QComboBox(this);
    m_hourCombo->setStyleSheet(kField); m_hourCombo->setFixedWidth(80);
    for (int h = 1; h <= 12; ++h) m_hourCombo->addItem(QString::number(h));

    m_minuteCombo = new QComboBox(this);
    m_minuteCombo->setStyleSheet(kField); m_minuteCombo->setFixedWidth(74);
    for (int m = 0; m < 60; ++m) m_minuteCombo->addItem(QString("%1").arg(m, 2, 10, QChar('0')));

    m_ampmCombo = new QComboBox(this);
    m_ampmCombo->setStyleSheet(kField); m_ampmCombo->setFixedWidth(74);
    m_ampmCombo->addItem("AM"); m_ampmCombo->addItem("PM");

    QTime initTime = data.customAirTime.isValid() ? data.customAirTime : QTime(0,0);
    int h12 = initTime.hour() % 12; if (h12==0) h12=12;
    m_hourCombo->setCurrentIndex(h12 - 1);
    m_minuteCombo->setCurrentIndex(initTime.minute());
    m_ampmCombo->setCurrentIndex(initTime.hour() >= 12 ? 1 : 0);

    timeRow->addWidget(new QLabel("Hour:", this));
    timeRow->addWidget(m_hourCombo);
    timeRow->addWidget(new QLabel(":", this));
    timeRow->addWidget(m_minuteCombo);
    timeRow->addWidget(m_ampmCombo);
    timeRow->addStretch();
    auto* rTimeBtn = new QPushButton("Reset", this);
    rTimeBtn->setStyleSheet(kReset); rTimeBtn->setFixedWidth(64);
    rTimeBtn->setToolTip("Reset to midnight");
    connect(rTimeBtn, &QPushButton::clicked, this, &EditTileDialog::onResetTime);
    timeRow->addWidget(rTimeBtn);
    vlay->addLayout(timeRow);

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
    selBtn->setStyleSheet("QPushButton { background:#2a2a2a; color:#ccc; border:1px solid #444; "
        "border-radius:4px; padding:8px 16px; font-size:13px; } QPushButton:hover { background:#383838; }");
    connect(selBtn, &QPushButton::clicked, this, &EditTileDialog::onSelectImage);
    imgRow->addWidget(selBtn);
    auto* rImgBtn = new QPushButton("Reset Image", this);
    rImgBtn->setStyleSheet(kReset);
    connect(rImgBtn, &QPushButton::clicked, this, &EditTileDialog::onResetImage);
    imgRow->addWidget(rImgBtn);
    imgRow->addStretch();
    vlay->addLayout(imgRow);

    // ── Bottom buttons ────────────────────────────────────────────────────────
    auto* sep2 = new QFrame(this);
    sep2->setFrameShape(QFrame::HLine); sep2->setStyleSheet("color:#2a2a2a;");
    vlay->addWidget(sep2);
    auto* btnRow = new QHBoxLayout; btnRow->setSpacing(8);
    auto* removeBtn = new QPushButton("Remove Tile", this);
    removeBtn->setStyleSheet("QPushButton { background:#4a1515; color:#ff8888; "
        "border:1px solid #662222; border-radius:4px; padding:8px 16px; font-size:13px; }"
        "QPushButton:hover { background:#661818; }");
    connect(removeBtn, &QPushButton::clicked, this, &EditTileDialog::onRemove);
    btnRow->addWidget(removeBtn);

    auto* testNotifBtn = new QPushButton("🔔  Test Notification", this);
    testNotifBtn->setStyleSheet(
        "QPushButton { background:#1a2a1a; color:#88dd88; "
        "border:1px solid #336633; border-radius:4px; padding:8px 16px; font-size:13px; }"
        "QPushButton:hover { background:#223322; }");
    testNotifBtn->setToolTip("Send a test notification for this tile via the tray notifier");
    connect(testNotifBtn, &QPushButton::clicked, this, [this](){
        emit testNotificationRequested();
    });
    btnRow->addWidget(testNotifBtn);

    btnRow->addStretch();
    auto* cancelBtn = new QPushButton("Cancel", this);
    cancelBtn->setStyleSheet("QPushButton { background:#252525; color:#aaa; "
        "border:1px solid #444; border-radius:4px; padding:8px 20px; font-size:13px; }"
        "QPushButton:hover { background:#333; }");
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    btnRow->addWidget(cancelBtn);
    auto* saveBtn = new QPushButton("Save", this);
    saveBtn->setStyleSheet("QPushButton { background:#0078d4; color:#fff; border:none; "
        "border-radius:4px; padding:8px 28px; font-size:13px; font-weight:bold; }"
        "QPushButton:hover { background:#1a8de4; }");
    connect(saveBtn, &QPushButton::clicked, this, &EditTileDialog::onSave);
    btnRow->addWidget(saveBtn);
    vlay->addLayout(btnRow);
}

// =============================================================================
QDate EditTileDialog::customDate() const
{
    if (!m_dateCheck->isChecked()) return QDate();  // explicit "no date"
    QDate chosen = m_dateEdit->date();
    // If matches TMDB date exactly and no custom was previously set → no override
    return (chosen == m_data.targetDate && !m_data.customDate.isValid())
           ? QDate() : chosen;
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
    // midnight (0:00) with no minutes = invalid (no override)
    return (h24 == 0 && mins == 0) ? QTime() : QTime(h24, mins);
}

// =============================================================================
void EditTileDialog::onDateToggled(bool enabled)
{
    m_dateEdit->setEnabled(enabled);
    m_dateEdit->setStyleSheet(enabled ? kField : kDisabledField);
}

void EditTileDialog::onResetTitle()
{
    m_titleReset = true;
    m_titleEdit->setText(m_data.title);
}

void EditTileDialog::onResetDate()
{
    m_dateReset = true;
    // Restore to TMDB date if valid; otherwise uncheck the date box
    if (m_data.targetDate.isValid()) {
        m_dateCheck->setChecked(true);
        m_dateEdit->setDate(m_data.targetDate);
    } else {
        m_dateCheck->setChecked(false);
    }
}

void EditTileDialog::onResetTime()
{
    m_timeReset = true;
    m_hourCombo->setCurrentIndex(11);    // 12
    m_minuteCombo->setCurrentIndex(0);   // :00
    m_ampmCombo->setCurrentIndex(0);     // AM → midnight
}

void EditTileDialog::onResetImage()
{
    m_imageReset = true;
    m_imagePath  = "";
    m_pathLabel->setText("Will re-download TMDB backdrop on save");
    m_previewLabel->clear();
    m_previewLabel->setStyleSheet(
        "QLabel { background:#111; border:1px solid #3a3a3a; border-radius:4px; }");
}

void EditTileDialog::onSelectImage()
{
    QString path = QFileDialog::getOpenFileName(
        this, "Select Backdrop Image", QString(),
        "Images (*.png *.jpg *.jpeg *.bmp *.webp)");
    if (path.isEmpty()) return;

    QString ext  = QFileInfo(path).suffix().toLower();
    QString dir  = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/custom_images";
    QDir().mkpath(dir);
    QString dest = dir + "/" + m_data.id + "_custom." + ext;
    if (QFile::exists(dest)) QFile::remove(dest);
    m_imagePath  = QFile::copy(path, dest) ? dest : path;
    m_imageReset = false;
    updatePreview(m_imagePath);
    m_pathLabel->setText(m_imagePath);
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
