#pragma once
#include <QString>
#include <QDate>
#include <QTime>

// Notification lifecycle:
//   Active   — tile counting down, notification not yet sent
//   Ready    — tile just expired; notifier will fire then flip to Inactive
//   Inactive — notification sent; never re-fires
enum class NotifStatus { Active, Ready, Inactive };

struct TileData
{
    QString id;
    QString title;
    QString customTitle;
    int     tmdbId    = 0;
    QString mediaType;
    QString tmdbUrl;
    QString statusLabel;
    QString rawDateText;
    QString dateDisplay;
    QString customDateStr;
    QDate   targetDate;
    QDate   customDate;
    QTime   airTime;
    QTime   customAirTime;
    QString imagePath;
    bool    notified    = false;
    NotifStatus notifStatus = NotifStatus::Active;
    bool    isLooped       = false;
    bool    noDateOverride = false;   // user explicitly removed the date — survives TMDB refresh
    QString presetType;
    QString loopInterval;      // "Yearly" | "Monthly" | "Weekly" | "Daily"
    int     loopWeekday    = 1; // 1=Mon..7=Sun (Qt::DayOfWeek), used when Weekly
    int     loopDayOfMonth = 1; // 1-31, used when Monthly

    QString displayTitle()  const { return customTitle.isEmpty() ? title : customTitle; }
    QString displayDate()   const { return customDateStr.isEmpty() ? dateDisplay : customDateStr; }
    QDate   effectiveDate() const { return customDate.isValid() ? customDate : targetDate; }
    QTime   effectiveTime() const { return customAirTime.isValid() ? customAirTime : airTime; }

    bool isExpired() const {
        QDate d = effectiveDate();
        if (!d.isValid()) return false;
        if (d < QDate::currentDate()) return true;
        if (d > QDate::currentDate()) return false;
        // d == today: use midnight as default when no specific air time is set.
        // This matches the countdown widget which also defaults to QTime(0,0,0).
        // Without this fix, tiles with "today" + no time show countdown=0 but
        // never move to the Released tab because isExpired() returned false.
        QTime t = effectiveTime().isValid() ? effectiveTime() : QTime(0, 0, 0);
        return QTime::currentTime() >= t;
    }
    bool hasDate()       const { return !noDateOverride && effectiveDate().isValid(); }
    int  daysRemaining() const { return QDate::currentDate().daysTo(effectiveDate()); }
    bool isValid()       const { return !id.isEmpty() && !title.isEmpty(); }
};
