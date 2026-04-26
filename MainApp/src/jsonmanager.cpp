#include "jsonmanager.h"
#include <QStandardPaths>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>

JsonManager& JsonManager::instance()
{
    static JsonManager inst;
    return inst;
}

QString JsonManager::dataFilePath() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/tiles.json";
}

QList<TileData> JsonManager::loadTiles() const
{
    QList<TileData> result;
    QFile f(dataFilePath());
    if (!f.open(QIODevice::ReadOnly)) return result;
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    if (!doc.isArray()) return result;

    for (const QJsonValue& v : doc.array()) {
        QJsonObject o = v.toObject();
        TileData td;
        td.id            = o["id"].toString();
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
        // customAirTime stored as minutes-since-midnight integer (e.g. 21*60=1260 for 9pm)
        // -1 means not set (no override)
        int airMins = o["customAirMins"].toInt(-1);
        td.customAirTime = (airMins >= 0 && airMins < 1440)
            ? QTime(airMins / 60, airMins % 60)
            : QTime();   // invalid = no override
        td.imagePath     = o["imagePath"].toString();
        td.mediaType     = o["mediaType"].toString();
        td.notified      = o["notified"].toBool(false);

        // notifStatus: "Active" / "Ready" / "Inactive"
        QString ns = o["notifStatus"].toString("Active");
        if (ns == "Inactive")     td.notifStatus = NotifStatus::Inactive;
        else if (ns == "Ready")   td.notifStatus = NotifStatus::Ready;
        else                      td.notifStatus = NotifStatus::Active;
        // Legacy migration: if old notified==true, treat as Inactive
        if (td.notified && td.notifStatus == NotifStatus::Active)
            td.notifStatus = NotifStatus::Inactive;

        // Sanitise legacy status labels
        if (td.statusLabel == "Returning Series" || td.statusLabel == "Ended")
            td.statusLabel = "Last Episode";

        if (!td.id.isEmpty()) result.append(td);
    }
    return result;
}

bool JsonManager::saveTiles(const QList<TileData>& tiles) const
{
    QDir().mkpath(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
    QJsonArray arr;
    for (const TileData& td : tiles) {
        QJsonObject o;
        o["id"]            = td.id;
        o["title"]         = td.title;
        o["customTitle"]   = td.customTitle;
        o["tmdbId"]        = td.tmdbId;
        o["mediaType"]     = td.mediaType;
        o["tmdbUrl"]       = td.tmdbUrl;
        o["statusLabel"]   = td.statusLabel;
        o["rawDateText"]   = td.rawDateText;
        o["dateDisplay"]   = td.dateDisplay;
        o["customDateStr"] = td.customDateStr;
        o["targetDate"]    = td.targetDate.toString(Qt::ISODate);
        o["customDate"]    = td.customDate.isValid() ? td.customDate.toString(Qt::ISODate) : "";
        o["airTime"]       = td.airTime.isValid() ? td.airTime.toString("HH:mm") : "";
        // Store as minutes-since-midnight integer; -1 = not set
        o["customAirMins"] = td.customAirTime.isValid()
            ? (td.customAirTime.hour() * 60 + td.customAirTime.minute())
            : -1;
        o["imagePath"]     = td.imagePath;
        o["notified"]      = td.notified;
        // notifStatus
        QString ns;
        switch (td.notifStatus) {
            case NotifStatus::Inactive: ns = "Inactive"; break;
            case NotifStatus::Ready:    ns = "Ready";    break;
            default:                    ns = "Active";   break;
        }
        o["notifStatus"]   = ns;
        arr.append(o);
    }
    QFile f(dataFilePath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    f.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
    return true;
}
