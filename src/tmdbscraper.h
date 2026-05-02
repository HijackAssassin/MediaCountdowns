#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QList>
#include "tiledata.h"

struct SearchResult {
    int     id         = 0;
    QString mediaType;
    QString title;
    int     year       = 0;
    QString posterPath;
    QString director;
    QString castLine;
    bool    isUS       = false;
    double  popularity = 0.0;   // TMDB popularity score — higher = more popular
};

class TmdbScraper : public QObject
{
    Q_OBJECT
public:
    explicit TmdbScraper(QObject* parent = nullptr);

    void searchMedia(const QString& query);
    void fetchDetails(int tmdbId, const QString& mediaType, const QString& posterPath = {});
    void fetchCreditsForResults(const QList<SearchResult>& results);

    // Called on startup to refresh each saved tile's date/backdrop
    void refreshTile(const TileData& existing);

signals:
    void searchResultsReady(const QList<SearchResult>& results);
    void creditsReady(int tmdbId, const QString& director, const QString& castLine);
    void dataReady(const TileData& data);
    void tileRefreshed(const TileData& updated);   // startup refresh result
    void posterReady(const QString& tileId, const QString& localPath);
    void scraperError(const QString& message);

private:
    QNetworkReply* getJson(const QString& url);
    void downloadBackdrop(const QString& tileId, const QString& backdropPath);
    void fetchSeasonForMultiEp(int showId, int season, const QString& airDate,
                                const TileData& td, bool isRefresh);
    TileData parseDetailsJson(const QJsonObject& obj, const QString& mediaType,
                              const QString& existingId = {}, const QString& existingImage = {});

    QNetworkAccessManager* m_nam;

    static constexpr const char* API_KEY = "693ee361bc407ffa8973abcd76d80120";
    static constexpr const char* BASE    = "https://api.themoviedb.org/3";
};
