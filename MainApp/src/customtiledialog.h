#pragma once
#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QDateEdit>
#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include "tiledata.h"

class CustomTileDialog : public QDialog
{
    Q_OBJECT
public:
    explicit CustomTileDialog(QWidget* parent = nullptr);
    TileData result() const { return m_result; }

private slots:
    void onSelectImage();
    void onClearImage();
    void onDateToggled(bool enabled);
    void onSave();

private:
    void updatePreview(const QString& path);
    QString copyImageToAppFolder(const QString& srcPath);

    TileData     m_result;
    QString      m_imagePath;

    QLabel*      m_previewLabel = nullptr;
    QLabel*      m_pathLabel    = nullptr;
    QLineEdit*   m_titleEdit    = nullptr;
    QCheckBox*   m_dateCheck    = nullptr;
    QDateEdit*   m_dateEdit     = nullptr;
    QComboBox*   m_hourCombo    = nullptr;
    QComboBox*   m_minuteCombo  = nullptr;
    QComboBox*   m_ampmCombo    = nullptr;
};
