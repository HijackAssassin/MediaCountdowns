#include "customtiledialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QUuid>
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

static const char* kSection = "color:#888; font-size:11px; margin-top:4px;";

CustomTileDialog::CustomTileDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("Create Custom Tile");
    setFixedWidth(540);
    setModal(true);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    auto* vlay = new QVBoxLayout(this);
    vlay->setContentsMargins(20, 20, 20, 20);
    vlay->setSpacing(8);

    auto addLabel = [&](const QString& t) {
        auto* l = new QLabel(t, this); l->setStyleSheet(kSection); vlay->addWidget(l);
    };

    // ── Title ─────────────────────────────────────────────────────────────────
    addLabel("Title  (required)");
    m_titleEdit = new QLineEdit(this);
    m_titleEdit->setStyleSheet(kField);
    m_titleEdit->setPlaceholderText("e.g.  My Birthday,  Movie Night…");
    vlay->addWidget(m_titleEdit);

    // ── Date ──────────────────────────────────────────────────────────────────
    addLabel("Date");
    m_dateCheck = new QCheckBox("Has a release / event date", this);
    m_dateCheck->setStyleSheet("color:#cccccc; font-size:13px;");
    m_dateCheck->setChecked(true);
    connect(m_dateCheck, &QCheckBox::toggled, this, &CustomTileDialog::onDateToggled);
    vlay->addWidget(m_dateCheck);

    auto* dateRow = new QHBoxLayout; dateRow->setSpacing(6);
    m_dateEdit = new QDateEdit(this);
    m_dateEdit->setCalendarPopup(true);
    m_dateEdit->setDisplayFormat("MMMM d, yyyy");
    m_dateEdit->setStyleSheet(kField);
    m_dateEdit->setMinimumDate(QDate(2000,1,1));
    m_dateEdit->setMaximumDate(QDate(2099,12,31));
    m_dateEdit->setDate(QDate::currentDate());
    dateRow->addWidget(m_dateEdit, 1);
    vlay->addLayout(dateRow);

    // ── Time ─────────────────────────────────────────────────────────────────
    addLabel("Time  (optional — defaults to midnight)");
    auto* timeRow = new QHBoxLayout; timeRow->setSpacing(6);
    m_hourCombo = new QComboBox(this);
    m_hourCombo->setStyleSheet(kField); m_hourCombo->setFixedWidth(80);
    for (int h = 1; h <= 12; ++h) m_hourCombo->addItem(QString::number(h));
    m_hourCombo->setCurrentIndex(11);  // 12 AM = midnight

    m_minuteCombo = new QComboBox(this);
    m_minuteCombo->setStyleSheet(kField); m_minuteCombo->setFixedWidth(74);
    for (int m = 0; m < 60; ++m) m_minuteCombo->addItem(QString("%1").arg(m, 2, 10, QChar('0')));

    m_ampmCombo = new QComboBox(this);
    m_ampmCombo->setStyleSheet(kField); m_ampmCombo->setFixedWidth(74);
    m_ampmCombo->addItem("AM"); m_ampmCombo->addItem("PM");

    timeRow->addWidget(new QLabel("Hour:", this));
    timeRow->addWidget(m_hourCombo);
    timeRow->addWidget(new QLabel(":", this));
    timeRow->addWidget(m_minuteCombo);
    timeRow->addWidget(m_ampmCombo);
    timeRow->addStretch();
    vlay->addLayout(timeRow);

    // ── Divider ───────────────────────────────────────────────────────────────
    auto* sep = new QFrame(this);
    sep->setFrameShape(QFrame::HLine); sep->setStyleSheet("color:#2a2a2a;");
    vlay->addWidget(sep);

    // ── Image ─────────────────────────────────────────────────────────────────
    addLabel("Backdrop Image  (optional)");
    m_previewLabel = new QLabel(this);
    m_previewLabel->setFixedSize(500, 281);
    m_previewLabel->setAlignment(Qt::AlignCenter);
    m_previewLabel->setStyleSheet(
        "QLabel { background:#000; border:1px solid #3a3a3a; border-radius:4px; }");
    vlay->addWidget(m_previewLabel);
    m_pathLabel = new QLabel("No image selected", this);
    m_pathLabel->setStyleSheet("color:#555; font-size:10px;");
    vlay->addWidget(m_pathLabel);

    auto* imgRow = new QHBoxLayout; imgRow->setSpacing(8);
    auto* selBtn = new QPushButton("Select Image…", this);
    selBtn->setStyleSheet("QPushButton { background:#2a2a2a; color:#ccc; border:1px solid #444; "
        "border-radius:4px; padding:8px 16px; font-size:13px; } QPushButton:hover { background:#383838; }");
    connect(selBtn, &QPushButton::clicked, this, &CustomTileDialog::onSelectImage);
    imgRow->addWidget(selBtn);
    auto* clearBtn = new QPushButton("Clear", this);
    clearBtn->setStyleSheet("QPushButton { background:#2a2a2a; color:#aaa; border:1px solid #3a3a3a; "
        "border-radius:4px; padding:8px 14px; font-size:12px; } QPushButton:hover { background:#383838; color:#fff; }");
    connect(clearBtn, &QPushButton::clicked, this, &CustomTileDialog::onClearImage);
    imgRow->addWidget(clearBtn);
    imgRow->addStretch();
    vlay->addLayout(imgRow);

    // ── Buttons ───────────────────────────────────────────────────────────────
    auto* sep2 = new QFrame(this);
    sep2->setFrameShape(QFrame::HLine); sep2->setStyleSheet("color:#2a2a2a;");
    vlay->addWidget(sep2);
    auto* btnRow = new QHBoxLayout; btnRow->setSpacing(8);
    btnRow->addStretch();
    auto* cancelBtn = new QPushButton("Cancel", this);
    cancelBtn->setStyleSheet("QPushButton { background:#252525; color:#aaa; border:1px solid #444; "
        "border-radius:4px; padding:8px 20px; font-size:13px; } QPushButton:hover { background:#333; }");
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    btnRow->addWidget(cancelBtn);
    auto* saveBtn = new QPushButton("Create Tile", this);
    saveBtn->setStyleSheet("QPushButton { background:#0078d4; color:#fff; border:none; "
        "border-radius:4px; padding:8px 28px; font-size:13px; font-weight:bold; } QPushButton:hover { background:#1a8de4; }");
    connect(saveBtn, &QPushButton::clicked, this, &CustomTileDialog::onSave);
    btnRow->addWidget(saveBtn);
    vlay->addLayout(btnRow);
}

void CustomTileDialog::onDateToggled(bool enabled)
{
    m_dateEdit->setEnabled(enabled);
    m_dateEdit->setStyleSheet(enabled ? kField : kDisabledField);
    m_hourCombo->setEnabled(enabled);
    m_minuteCombo->setEnabled(enabled);
    m_ampmCombo->setEnabled(enabled);
}

void CustomTileDialog::onSelectImage()
{
    QString path = QFileDialog::getOpenFileName(
        this, "Select Backdrop Image", QString(),
        "Images (*.png *.jpg *.jpeg *.bmp *.webp)");
    if (path.isEmpty()) return;
    QString copied = copyImageToAppFolder(path);
    m_imagePath = copied.isEmpty() ? path : copied;
    updatePreview(m_imagePath);
    m_pathLabel->setText(m_imagePath);
}

void CustomTileDialog::onClearImage()
{
    if (!m_imagePath.isEmpty() &&
        (m_imagePath.contains("/custom_images/") || m_imagePath.contains("\\custom_images\\")))
        QFile::remove(m_imagePath);
    m_imagePath.clear();
    m_previewLabel->clear();
    m_previewLabel->setStyleSheet(
        "QLabel { background:#000; border:1px solid #3a3a3a; border-radius:4px; }");
    m_pathLabel->setText("No image selected");
}

void CustomTileDialog::onSave()
{
    if (m_titleEdit->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, "Title Required", "Please enter a title for this tile.");
        return;
    }

    QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);

    int h12  = m_hourCombo->currentIndex() + 1;
    int mins = m_minuteCombo->currentIndex();
    bool pm  = (m_ampmCombo->currentIndex() == 1);
    int h24  = (h12 % 12) + (pm ? 12 : 0);
    QTime t  = (h24 == 0 && mins == 0) ? QTime() : QTime(h24, mins);

    bool hasDate = m_dateCheck->isChecked();
    QDate date   = hasDate ? m_dateEdit->date() : QDate();

    m_result.id            = id;
    m_result.tmdbId        = 0;
    m_result.mediaType     = "custom";
    m_result.title         = m_titleEdit->text().trimmed();
    m_result.customTitle   = "";
    m_result.targetDate    = date;
    m_result.dateDisplay   = date.isValid() ? date.toString("MMMM d, yyyy") : "";
    m_result.customAirTime = t;
    m_result.imagePath     = m_imagePath;
    m_result.statusLabel   = "";
    m_result.notified      = false;

    // Rename temp image to final tile id
    if (!m_imagePath.isEmpty() &&
        (m_imagePath.contains("/custom_images/") || m_imagePath.contains("\\custom_images\\"))) {
        QFileInfo fi(m_imagePath);
        QString newPath = fi.absolutePath() + "/" + id + "_custom." + fi.suffix().toLower();
        if (QFile::rename(m_imagePath, newPath)) m_result.imagePath = newPath;
    }

    accept();
}

QString CustomTileDialog::copyImageToAppFolder(const QString& srcPath)
{
    QString ext  = QFileInfo(srcPath).suffix().toLower();
    QString dir  = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/custom_images";
    QDir().mkpath(dir);
    QString dest = dir + "/tmp_" + QUuid::createUuid().toString(QUuid::WithoutBraces) + "." + ext;
    return QFile::copy(srcPath, dest) ? dest : QString();
}

void CustomTileDialog::updatePreview(const QString& path)
{
    if (path.isEmpty()) { m_previewLabel->clear(); return; }
    QPixmap px(path);
    if (px.isNull()) return;
    m_previewLabel->setPixmap(
        px.scaled(500, 281, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
}
