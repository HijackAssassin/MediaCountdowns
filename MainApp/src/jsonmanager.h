#pragma once
#include <QList>
#include <QString>
#include "tiledata.h"

// =============================================================================
//  JsonManager – singleton that reads/writes tiles.json
//                Location:  %APPDATA%\CineCountdown\tiles.json  (Windows)
// =============================================================================
class JsonManager
{
public:
    static JsonManager& instance();

    QList<TileData> loadTiles() const;
    bool            saveTiles(const QList<TileData>& tiles) const;
    QString         dataFilePath() const;

private:
    JsonManager() = default;
    JsonManager(const JsonManager&) = delete;
    JsonManager& operator=(const JsonManager&) = delete;
};
