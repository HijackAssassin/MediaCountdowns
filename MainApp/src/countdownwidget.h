#pragma once
#include <QWidget>
#include <QGridLayout>
#include <QDateTime>
#include "outlinedlabel.h"

// =============================================================================
//  CountdownWidget — label-based countdown display.
//
//  Uses individual OutlinedLabel widgets for each number and unit label,
//  exactly like EventCountdown v2 which handles 30+ tiles without lag.
//
//  The key performance win: Qt only repaints the specific label whose text
//  changed. Seconds repaint every tick; minutes repaint once per minute;
//  hours/days rarely. The old approach (single widget, full repaint every
//  second via QPainterPath) was O(tiles) work per second just for painting.
// =============================================================================
class CountdownWidget : public QWidget
{
    Q_OBJECT
public:
    explicit CountdownWidget(QWidget* parent = nullptr) : QWidget(parent)
    {
        setAttribute(Qt::WA_TranslucentBackground);
        setStyleSheet("background: transparent;");

        auto* grid = new QGridLayout(this);
        grid->setHorizontalSpacing(0);
        grid->setVerticalSpacing(0);
        grid->setContentsMargins(0, 0, 0, 0);
        grid->setAlignment(Qt::AlignCenter);

        // Number labels (row 0) — large bold white outlined text
        m_nums[0] = makeNum("0");
        m_nums[1] = makeNum("00");
        m_nums[2] = makeNum("00");
        m_nums[3] = makeNum("00");

        // Unit labels (row 1) — small white outlined text
        const char* units[] = { "days", "hours", "minutes", "seconds" };
        for (int i = 0; i < 4; ++i) {
            m_units[i] = makeUnit(units[i]);
            grid->addWidget(m_nums[i],  0, i, Qt::AlignCenter);
            grid->addWidget(m_units[i], 1, i, Qt::AlignCenter);
        }

        // Message label for no-date / expired states (hidden by default)
        m_msg = new OutlinedLabel(this);
        m_msg->setAlignment(Qt::AlignCenter);
        QFont mf; mf.setPointSize(14); mf.setBold(true);
        m_msg->setFont(mf);
        QPalette mp = m_msg->palette();
        mp.setColor(QPalette::WindowText, QColor(0xaa, 0xaa, 0xaa));
        m_msg->setPalette(mp);
        m_msg->hide();
        grid->addWidget(m_msg, 0, 0, 2, 4, Qt::AlignCenter);
    }

    // Called by TileWidget::tick() — simply update the labels that changed.
    void setSeconds(qint64 secs)
    {
        if (secs <= 0) { setExpired(); return; }

        showCountdown();

        qint64 d = secs / 86400;
        qint64 h = (secs % 86400) / 3600;
        qint64 m = (secs % 3600) / 60;
        qint64 s = secs % 60;

        // Only call setText when the value actually changed — Qt only repaints
        // the label that changed, so seconds repaints every tick but
        // hours/days/minutes only repaint when they change.
        setNum(m_nums[0], QString::number(d),            m_last[0]);
        setNum(m_nums[1], QString("%1").arg(h,2,10,QChar('0')), m_last[1]);
        setNum(m_nums[2], QString("%1").arg(m,2,10,QChar('0')), m_last[2]);
        setNum(m_nums[3], QString("%1").arg(s,2,10,QChar('0')), m_last[3]);
    }

    void setNoDate()  { hideAll(); }
    void setExpired() { hideAll(); }

    // Called when tile is resized — scale font sizes to tile width
    void scaleFonts(int tileWidth)
    {
        if (tileWidth == m_lastScaleWidth) return;
        m_lastScaleWidth = tileWidth;
        int numPt  = qMax(7, tileWidth / 12);
        int unitPt = qMax(3, tileWidth / 42);
        for (int i = 0; i < 4; ++i) {
            QFont fn = m_nums[i]->font();
            fn.setPointSize(numPt);
            fn.setWeight(QFont::Bold);
            fn.setLetterSpacing(QFont::AbsoluteSpacing, qMax(1, tileWidth / 80));
            m_nums[i]->setFont(fn);
            QFont fu = m_units[i]->font();
            fu.setPointSize(unitPt);
            fu.setBold(false);
            m_units[i]->setFont(fu);
        }
    }

private:
    OutlinedLabel* m_nums[4]  = {};
    OutlinedLabel* m_units[4] = {};
    OutlinedLabel* m_msg      = nullptr;
    qint64         m_last[4]  = {-1,-1,-1,-1};  // track last displayed values
    int            m_lastScaleWidth = -1;        // guard scaleFonts redundant calls

    static OutlinedLabel* makeNum(const QString& text) {
        auto* l = new OutlinedLabel(text);
        l->setAlignment(Qt::AlignCenter);
        l->setAttribute(Qt::WA_TranslucentBackground);
        // Use font + palette directly — no stylesheet parsing on every tile
        QFont f;
        f.setPointSize(35);
        f.setWeight(QFont::Bold);
        f.setLetterSpacing(QFont::AbsoluteSpacing, 4); // space out the digits
        l->setFont(f);
        QPalette pal = l->palette();
        pal.setColor(QPalette::WindowText, Qt::white);
        l->setPalette(pal);
        // Padding via margins so it still spaces correctly in the grid
        l->setContentsMargins(6, 0, 6, 0);
        return l;
    }
    static OutlinedLabel* makeUnit(const QString& text) {
        auto* l = new OutlinedLabel(text);
        l->setAlignment(Qt::AlignCenter);
        l->setAttribute(Qt::WA_TranslucentBackground);
        QFont f;
        f.setPointSize(7);
        f.setLetterSpacing(QFont::AbsoluteSpacing, 1);
        l->setFont(f);
        QPalette pal = l->palette();
        pal.setColor(QPalette::WindowText, QColor(0xdd, 0xdd, 0xdd));
        l->setPalette(pal);
        l->setContentsMargins(4, 0, 4, 0);
        return l;
    }

    inline void setNum(OutlinedLabel* lbl, const QString& val, qint64& last) {
        qint64 v = val.toLongLong();
        if (v == last) return;
        last = v;
        lbl->setText(val);
    }

    void hideAll() {
        for (int i = 0; i < 4; ++i) { m_nums[i]->hide(); m_units[i]->hide(); }
        m_msg->hide();
    }
    void showCountdown() {
        m_msg->hide();
        for (int i = 0; i < 4; ++i) { m_nums[i]->show(); m_units[i]->show(); }
    }
    void showMsg(const QString& txt) {
        for (int i = 0; i < 4; ++i) { m_nums[i]->hide(); m_units[i]->hide(); }
        m_msg->setText(txt);
        m_msg->show();
    }
};
