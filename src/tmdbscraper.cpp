#include "tmdbscraper.h"
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUuid>
#include <QRegularExpression>
#include <QFile>
#include <QDir>
#include <QStandardPaths>
#include <QTimeZone>
#include <QDebug>
#include <algorithm>

TmdbScraper::TmdbScraper(QObject* parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{}

// =============================================================================
//  Phase 1 — search with improved filtering
// =============================================================================
void TmdbScraper::searchMedia(const QString& query)
{
    QString q = query.trimmed();
    // Normalize common hyphenated titles
    q.replace(QRegularExpression("\\bspiderman\\b", QRegularExpression::CaseInsensitiveOption), "spider-man");
    q.replace(QRegularExpression("\\bxmen\\b",      QRegularExpression::CaseInsensitiveOption), "x-men");

    // Marvel alias map — used in filter below, NOT to rewrite the API query
    // so that searching "punisher" still returns The Punisher (1989) etc.
    static const QVector<QPair<QString,QString>> kMarvelAliases = {
        {"daredevil",               "Marvel's Daredevil"},
        {"the punisher",            "Marvel's The Punisher"},
        {"punisher",                "Marvel's The Punisher"},
        {"jessica jones",           "Marvel's Jessica Jones"},
        {"luke cage",               "Marvel's Luke Cage"},
        {"iron fist",               "Marvel's Iron Fist"},
        {"the defenders",           "Marvel's The Defenders"},
        {"runaways",                "Marvel's Runaways"},
        {"cloak and dagger",        "Marvel's Cloak & Dagger"},
        {"cloak & dagger",          "Marvel's Cloak & Dagger"},
        {"inhumans",                "Marvel's Inhumans"},
        {"agent carter",            "Marvel's Agent Carter"},
        {"agents of shield",        "Marvel's Agents of S.H.I.E.L.D."},
        {"agents of s.h.i.e.l.d.", "Marvel's Agents of S.H.I.E.L.D."},
        {"ultimate spider-man",     "Marvel's Ultimate Spider-Man"},
        {"ultimate spiderman",      "Marvel's Ultimate Spider-Man"},
        {"marvels avengers",        "Marvel's Avengers"},
    };
    // Find the Marvel full title that matches this query (if any)
    QString marvelFullTitle;
    for (const auto& alias : kMarvelAliases) {
        if (q.compare(alias.first, Qt::CaseInsensitive) == 0) {
            marvelFullTitle = alias.second;
            break;
        }
    }

    // Detect optional trailing year e.g. "Supergirl 2026"
    int yearFilter = 0;
    QRegularExpression yearRe(R"(\b(19\d{2}|20\d{2})$)");
    auto ym = yearRe.match(q);
    if (ym.hasMatch()) {
        yearFilter = ym.captured(1).toInt();
        q = q.left(ym.capturedStart()).trimmed();
    }

    // Fetch 2 pages to get more candidates before filtering (up to ~40 results)
    QString url1 = QString("%1/search/multi?api_key=%2&query=%3&include_adult=false&page=1")
        .arg(BASE, apiKey(), QString::fromUtf8(QUrl::toPercentEncoding(q)));
    QString url2 = QString("%1/search/multi?api_key=%2&query=%3&include_adult=false&page=2")
        .arg(BASE, apiKey(), QString::fromUtf8(QUrl::toPercentEncoding(q)));

    // Use a shared state for both pages
    struct PageState {
        QJsonArray combined;
        int        pagesReceived = 0;
    };
    auto state = QSharedPointer<PageState>::create();


    auto processPage = [this, state, q, yearFilter, marvelFullTitle](const QJsonArray& arr) {
        for (const QJsonValue& v : arr)
            state->combined.append(v);
        state->pagesReceived++;
        if (state->pagesReceived < 2) return;

        // ── Normalisation helpers ─────────────────────────────────────────────
        auto strip = [](const QString& s) -> QString {
            QString d = s.normalized(QString::NormalizationForm_D);
            QString r;
            for (const QChar& c : d)
                if (c.category() != QChar::Mark_NonSpacing) r += c;
            return r;
        };

        auto compact = [&strip](const QString& s) -> QString {
            QString r;
            for (const QChar& c : strip(s.toLower()))
                if (c.isLetterOrNumber()) r += c;
            return r;
        };

        auto words = [&strip](const QString& s) -> QStringList {
            QStringList out;
            QString replaced = s;
            replaced.replace('-', ' ').replace('_', ' ');
            for (const QString& w : strip(replaced.toLower()).split(' ', Qt::SkipEmptyParts)) {
                QString clean;
                for (const QChar& c : w)
                    if (c.isLetterOrNumber()) clean += c;
                if (!clean.isEmpty()) out << clean;
            }
            return out;
        };

        QStringList queryWords   = words(q);
        QString     queryCompact = compact(q);

        QSettings prefs("HijackAssassin", "MediaCountdowns");
        bool showMovies   = prefs.value("pref_movies",  true).toBool();
        bool showShows    = prefs.value("pref_shows",   true).toBool();
        bool showReality  = prefs.value("pref_reality", true).toBool();
        bool showDocs     = prefs.value("pref_docs",    true).toBool();
        bool showTalk     = prefs.value("pref_talk",    true).toBool();
        bool showForeign  = prefs.value("pref_foreign", true).toBool();

        QSet<int> blockedGenres;
        if (!showReality) blockedGenres << 10764;
        if (!showDocs)    blockedGenres << 99;
        if (!showTalk)    blockedGenres << 10767;

        QList<SearchResult> results;

        for (const QJsonValue& v : state->combined) {
            QJsonObject o = v.toObject();
            QString type = o["media_type"].toString();
            if (type != "movie" && type != "tv") continue;
            if (type == "movie" && !showMovies) continue;
            if (type == "tv"    && !showShows)  continue;

            QString rawTitle = (type == "movie") ? o["title"].toString()
                                                 : o["name"].toString();
            if (rawTitle.isEmpty()) continue;

            // ── Block podcasts and low-quality content ────────────────────────
            if (rawTitle.contains("podcast",     Qt::CaseInsensitive)) continue;
            if (rawTitle.contains("fan film",    Qt::CaseInsensitive)) continue;
            if (rawTitle.contains("fan-made",    Qt::CaseInsensitive)) continue;

            // ── Genre filter — block documentaries, talk shows, reality ───────
            bool hasBlockedGenre = false;
            for (const QJsonValue& g : o["genre_ids"].toArray()) {
                if (blockedGenres.contains(g.toInt())) { hasBlockedGenre = true; break; }
            }
            if (hasBlockedGenre) continue;

            // ── Original language must be English (unless foreign allowed) ─────
            if (!showForeign && o["original_language"].toString() != "en") continue;

            // ── Title matching — query words must all appear in title ──────────
            QStringList titleWords = words(rawTitle);
            bool wordMatch = true;
            for (const QString& qw : queryWords)
                if (!titleWords.contains(qw)) { wordMatch = false; break; }

            bool compactMatch = compact(rawTitle).contains(queryCompact);
            if (!wordMatch && !compactMatch) continue;

            // ── First-word anchor + prefix-sequence check ─────────────────────
            // Rules:
            // 1. The first meaningful word of the title must match the first word
            //    of the query (skipping leading articles in the title only when
            //    the query itself doesn't start with an article).
            // 2. All query words must appear as a contiguous prefix of the title
            //    word list (so "the boys" cannot match "The Napa Boys").
            //    Exception: when only compactMatch fired (e.g. "spiderman" →
            //    "Spider-Man") we skip the sequence check because the hyphen
            //    split makes the words unequal.
            if (!queryWords.isEmpty()) {
                static const QSet<QString> articles = {"the","a","an"};
                bool queryStartsWithArticle = articles.contains(queryWords[0]);

                // Build title word list, optionally skipping a leading article
                QStringList anchoredTitle = titleWords;
                if (!queryStartsWithArticle && !anchoredTitle.isEmpty()
                    && articles.contains(anchoredTitle[0]))
                    anchoredTitle.removeFirst();

                // First-word anchor — also check subtitle after " - ", " – ", or ": "
                // Also allow pass-through if this title matches a Marvel alias
                bool marvelMatch = !marvelFullTitle.isEmpty() &&
                                   rawTitle.compare(marvelFullTitle, Qt::CaseInsensitive) == 0;
                bool firstWordOk = marvelMatch ||
                                   (!anchoredTitle.isEmpty() && anchoredTitle[0] == queryWords[0]);
                if (!firstWordOk) {
                    QStringList toSplit = { rawTitle };
                    const QStringList seps = {" - ", " \u2013 ", " \u2014 ", ": "};
                    for (const QString& sep : seps) {
                        QStringList next;
                        for (const QString& s : toSplit) {
                            if (s.contains(sep)) {
                                QString sub = s.section(sep, 1).trimmed();
                                QStringList subWords = words(sub);
                                if (!queryStartsWithArticle && !subWords.isEmpty()
                                    && articles.contains(subWords[0]))
                                    subWords.removeFirst();
                                if (!subWords.isEmpty() && subWords[0] == queryWords[0])
                                    firstWordOk = true;
                                next << sub;
                            }
                        }
                        toSplit = next;
                        if (firstWordOk) break;
                    }
                }
                bool subtitleMatch = firstWordOk && !marvelMatch
                    && !anchoredTitle.isEmpty() && anchoredTitle[0] != queryWords[0];
                if (!firstWordOk) continue;

                // Prefix-sequence check (only when wordMatch — not compactMatch-only, not Marvel alias)
                // Ensures "the boys" doesn't match "The Napa Boys"
                if (wordMatch && !marvelMatch && !subtitleMatch && queryWords.size() > 1) {
                    bool seqOk = true;
                    for (int qi = 0; qi < queryWords.size(); ++qi) {
                        if (qi >= anchoredTitle.size() || anchoredTitle[qi] != queryWords[qi]) {
                            seqOk = false; break;
                        }
                    }
                    if (!seqOk) continue;
                }
            }

            // ── Year filter ───────────────────────────────────────────────────
            QString dateStr = (type == "movie") ? o["release_date"].toString()
                                                : o["first_air_date"].toString();
            int year = dateStr.left(4).toInt();
            if (yearFilter > 0 && year != yearFilter) continue;

            // ── Popularity floor — block very obscure results (fan films etc.) ─
            double popularity = o["popularity"].toDouble();
            if (popularity < 2.0) continue;

            SearchResult sr;
            sr.id         = o["id"].toInt();
            sr.mediaType  = type;
            sr.year       = year;
            sr.title      = (year > 0)
                ? QString("%1 (%2)").arg(rawTitle).arg(year)
                : rawTitle;
            sr.posterPath = o["poster_path"].toString();
            sr.isUS       = true;   // all results are US at this point
            sr.popularity = popularity;

            results.append(sr);
        }

        // ── Deduplicate by TMDB id ────────────────────────────────────────────
        QList<int> seen;
        results.erase(std::remove_if(results.begin(), results.end(),
            [&seen](const SearchResult& sr) {
                if (seen.contains(sr.id)) return true;
                seen.append(sr.id); return false;
            }), results.end());

        // ── Sort: newest year first, then by popularity within same year ──────
        std::sort(results.begin(), results.end(),
            [](const SearchResult& a, const SearchResult& b) {
                if (a.year != b.year) {
                    // No-year to end
                    if (a.year == 0) return false;
                    if (b.year == 0) return true;
                    return a.year > b.year;   // newest first
                }
                return a.popularity > b.popularity;
            });

        if (results.size() > 15) results = results.mid(0, 15);

        if (results.isEmpty()) {
            emit scraperError(
                yearFilter > 0
                ? QString("No results for that title in %1. Try without the year.").arg(yearFilter)
                : "No results found. Try a different spelling.");
            return;
        }
        emit searchResultsReady(results);
    };

    // Fire both page requests in parallel
    for (const QString& url : {url1, url2}) {
        QNetworkReply* r = getJson(url);
        connect(r, &QNetworkReply::finished, this, [r, processPage, this]() {
            r->deleteLater();
            if (r->error() != QNetworkReply::NoError) {
                emit scraperError("Search failed: " + r->errorString());
                return;
            }
            QJsonArray arr = QJsonDocument::fromJson(r->readAll())
                                 .object()["results"].toArray();
            processPage(arr);
        });
    }
}

// =============================================================================
//  parseDetailsJson  —  shared logic between fetchDetails and refreshTile
// =============================================================================
TileData TmdbScraper::parseDetailsJson(const QJsonObject& obj,
                                       const QString& mediaType,
                                       const QString& existingId,
                                       const QString& existingImage)
{
    TileData td;
    td.id        = existingId.isEmpty()
        ? QUuid::createUuid().toString(QUuid::WithoutBraces) : existingId;
    td.tmdbId    = obj["id"].toInt();
    td.mediaType = mediaType;
    td.tmdbUrl   = QString("https://www.themoviedb.org/%1/%2")
                       .arg(mediaType).arg(td.tmdbId);
    td.imagePath = existingImage;  // keep existing; backdrop download updates it

    // Title + year + season count
    QString rawTitle    = (mediaType == "tv") ? obj["name"].toString()
                                              : obj["title"].toString();
    QString dateForYear = (mediaType == "tv") ? obj["first_air_date"].toString()
                                              : obj["release_date"].toString();
    int year = dateForYear.left(4).toInt();

    // ── All-caps title fix (e.g. INVINCIBLE → Invincible) ────────────────────
    {
        bool allCaps = !rawTitle.isEmpty()
                    && rawTitle == rawTitle.toUpper()
                    && rawTitle.contains(QRegularExpression("[A-Z]{2}"));
        if (allCaps) {
            static const QStringList minor = {
                "a","an","the","and","but","or","nor","for","yet","so",
                "at","by","in","of","on","to","up","as","is"
            };
            QStringList ws = rawTitle.toLower().split(' ', Qt::SkipEmptyParts);
            for (int i = 0; i < ws.size(); ++i) {
                bool cap = (i == 0) || !minor.contains(ws[i]);
                if (cap && !ws[i].isEmpty()) ws[i][0] = ws[i][0].toUpper();
            }
            rawTitle = ws.join(' ');
        }
    }

    if (mediaType == "tv") {
        // Count only seasons that have actually aired
        QDate today = QDate::currentDate();
        int seasons = 0;
        for (const QJsonValue& sv : obj["seasons"].toArray()) {
            QJsonObject s = sv.toObject();
            if (s["season_number"].toInt() <= 0) continue;
            QString airStr = s["air_date"].toString();
            if (airStr.isEmpty()) continue;
            QDate airDate = QDate::fromString(airStr, Qt::ISODate);
            if (airDate.isValid() && airDate <= today) ++seasons;
        }
        if (seasons == 0) seasons = qMax(1, obj["number_of_seasons"].toInt() - 1);

        // No year in tile title — year is only shown in the search drop-up picker
        QString seasonStr = (seasons == 1) ? "1 Season" : QString("%1 Seasons").arg(seasons);
        td.title = QString("%1  \xe2\x80\xa2  %2").arg(rawTitle, seasonStr);
    } else {
        // Movie — just the name, no year
        td.title = rawTitle;
    }

    // ── Movie ─────────────────────────────────────────────────────────────────
    if (mediaType == "movie") {
        QString releaseDate;

        QJsonArray releaseResults = obj["release_dates"].toObject()["results"].toArray();
        bool isTheatrical = false;
        for (const QJsonValue& country : releaseResults) {
            if (country.toObject()["iso_3166_1"].toString() != "US") continue;
            QJsonArray dates = country.toObject()["release_dates"].toArray();

            // Priority: type 3 (Theatrical) > type 2 (Limited) > type 4 (Digital) > any
            int foundType = 0;
            for (int wantType : {3, 2, 4, 1, 5, 6}) {
                for (const QJsonValue& d : dates) {
                    if (d.toObject()["type"].toInt() == wantType) {
                        releaseDate = d.toObject()["release_date"].toString().left(10);
                        foundType   = wantType;
                        break;
                    }
                }
                if (!releaseDate.isEmpty()) break;
            }
            isTheatrical = (foundType == 3 || foundType == 2);
            break;
        }

        // Only fall back to global release_date if we truly found nothing for US
        if (releaseDate.isEmpty())
            releaseDate = obj["release_date"].toString();

        if (releaseDate.isEmpty()) {
            td.statusLabel = "No Release Date Yet";
            td.dateDisplay = "No Release Date Yet";
            return td;
        }

        td.targetDate = QDate::fromString(releaseDate, Qt::ISODate);

        // ── US early-screening adjustment ─────────────────────────────────────
        // US theaters typically run Thursday night previews the day before
        // official Friday release. Detect US timezone and subtract 1 day.
        QString tzId = QTimeZone::systemTimeZone().id();
        bool isUS = tzId.startsWith("America/") || tzId.startsWith("US/")
                 || tzId == "EST5EDT" || tzId == "CST6CDT"
                 || tzId == "MST7MDT" || tzId == "PST8PDT";
        if (isUS && isTheatrical && td.targetDate.isValid())
            td.targetDate = td.targetDate.addDays(-1);

        td.dateDisplay = td.targetDate.toString("MMMM d, yyyy");
        td.statusLabel = (td.targetDate > QDate::currentDate()) ? "Releases" : "Released";
    }

    // ── TV show ───────────────────────────────────────────────────────────────
    else {
        QJsonObject nextEp = obj["next_episode_to_air"].toObject();
        if (nextEp.isEmpty()) {
            QJsonObject lastEp = obj["last_episode_to_air"].toObject();
            td.statusLabel = "Last Episode";
            if (!lastEp.isEmpty()) {
                td.targetDate  = QDate::fromString(
                    lastEp["air_date"].toString(), Qt::ISODate);
                td.dateDisplay = td.targetDate.toString("MMMM d, yyyy");
                int lastSeason = lastEp["season_number"].toInt();
                int lastEpNum  = lastEp["episode_number"].toInt();
                if (lastSeason > 0 && lastEpNum > 0)
                    td.statusLabel = QString("S%1E%2")
                        .arg(lastSeason, 2, 10, QChar('0'))
                        .arg(lastEpNum,  2, 10, QChar('0'));
                if (lastSeason > 0)
                    td.rawDateText = QString("SCAN|%1").arg(lastSeason);
            } else {
                td.dateDisplay = "No Release Date Yet";
            }
        } else {
            QString airDate = nextEp["air_date"].toString();
            int season  = nextEp["season_number"].toInt();
            int episode = nextEp["episode_number"].toInt();
            td.targetDate  = QDate::fromString(airDate, Qt::ISODate);
            td.dateDisplay = td.targetDate.toString("MMMM d, yyyy");

            // Multi-episode detection: look in the season's episode list for
            // other episodes sharing the same air_date (premiere dumps / double bills)
            QString sLabel = QString("S%1E%2")
                .arg(season,  2, 10, QChar('0'))
                .arg(episode, 2, 10, QChar('0'));

            // Check if the full season data was included (it isn't by default,
            // but we scan next_episode_to_air's siblings if present).
            // We do a quick scan of any "seasons" array entries with the same date.
            // Real multi-ep detection happens via fetchSeasonForMultiEp called below.
            td.statusLabel = sLabel;
            // Store season/episode for post-process lookup
            td.rawDateText = QString("%1|%2|%3").arg(airDate).arg(season).arg(episode);

            // air_time from TMDB is unreliable (often wrong timezone/value)
            // Always leave td.airTime invalid — countdown targets midnight by default.
            // Users can set a custom time via the edit dialog.
        }
    }

    return td;
}

// =============================================================================
//  fetchSeasonForMultiEp  —  given a show id, season, airDate and a prepared
//  TileData, checks for multiple episodes on the same date and updates
//  statusLabel to "S04E01+E02" if needed, then emits the correct signal.
// =============================================================================
void TmdbScraper::fetchSeasonForMultiEp(int showId, int season,
                                         const QString& airDate,
                                         const TileData& td,
                                         bool isRefresh)
{
    QString url = QString("%1/tv/%2/season/%3?api_key=%4")
        .arg(BASE).arg(showId).arg(season).arg(apiKey());

    QNetworkReply* r = getJson(url);
    connect(r, &QNetworkReply::finished, this, [r, airDate, td, isRefresh, this]() mutable {
        r->deleteLater();

        if (r->error() == QNetworkReply::NoError) {
            QJsonArray eps = QJsonDocument::fromJson(r->readAll())
                                 .object()["episodes"].toArray();

            QList<int> sameDay;
            for (const QJsonValue& v : eps) {
                QJsonObject ep = v.toObject();
                if (ep["air_date"].toString() == airDate)
                    sameDay.append(ep["episode_number"].toInt());
            }

            if (sameDay.size() > 1) {
                std::sort(sameDay.begin(), sameDay.end());
                // Parse season from existing label e.g. "S04E01"
                int s = td.statusLabel.mid(1, 2).toInt();
                QString label = QString("S%1E%2")
                    .arg(s, 2, 10, QChar('0'))
                    .arg(sameDay[0], 2, 10, QChar('0'));
                for (int i = 1; i < sameDay.size(); ++i)
                    label += QString("+E%1").arg(sameDay[i], 2, 10, QChar('0'));
                const_cast<TileData&>(td).statusLabel = label;
            }
        }

        if (isRefresh) emit tileRefreshed(td);
        else           emit dataReady(td);
    });
}

// =============================================================================
//  fetchSeasonForFutureEp — called when next_episode_to_air is empty.
//  Scans the season episode list for any episode with a future air_date.
//  If found, updates the tile's targetDate and statusLabel to that episode.
// =============================================================================
void TmdbScraper::fetchSeasonForFutureEp(int showId, int season,
                                          TileData td, bool isRefresh)
{
    QString url = QString("%1/tv/%2/season/%3?api_key=%4")
        .arg(BASE).arg(showId).arg(season).arg(apiKey());

    QNetworkReply* r = getJson(url);
    connect(r, &QNetworkReply::finished, this, [r, td, isRefresh, this]() mutable {
        r->deleteLater();
        if (r->error() != QNetworkReply::NoError) {
            if (isRefresh) emit tileRefreshed(td); else emit dataReady(td);
            return;
        }
        QJsonArray eps = QJsonDocument::fromJson(r->readAll())
                             .object()["episodes"].toArray();
        QDate today = QDate::currentDate();
        QDate bestDate;
        int bestEp = -1, bestSeason = -1;

        for (const QJsonValue& v : eps) {
            QJsonObject ep = v.toObject();
            int epNum = ep["episode_number"].toInt();
            if (epNum <= 0) continue;
            QDate d = QDate::fromString(ep["air_date"].toString(), Qt::ISODate);
            if (!d.isValid() || d <= today) continue;
            if (!bestDate.isValid() || d < bestDate) {
                bestDate   = d;
                bestEp     = epNum;
                bestSeason = ep["season_number"].toInt();
            }
        }

        if (bestDate.isValid() && bestEp > 0) {
            // Found a future episode — update the tile
            td.targetDate  = bestDate;
            td.dateDisplay = bestDate.toString("MMMM d, yyyy");
            td.statusLabel = QString("S%1E%2")
                .arg(bestSeason, 2, 10, QChar('0'))
                .arg(bestEp,     2, 10, QChar('0'));
            td.rawDateText = QString("%1|%2|%3")
                .arg(bestDate.toString(Qt::ISODate)).arg(bestSeason).arg(bestEp);
        }
        // Emit regardless — if no future ep found the tile stays as "Last Episode"
        if (isRefresh) emit tileRefreshed(td); else emit dataReady(td);
    });
}

// =============================================================================
//  Phase 2 — fetch full details for a new tile
// =============================================================================
void TmdbScraper::fetchDetails(int tmdbId, const QString& mediaType, const QString&)
{
    QString appendTo = (mediaType == "tv") ? "next_episode_to_air,images" : "release_dates,images";
    QString url = QString("%1/%2/%3?api_key=%4&append_to_response=%5")
        .arg(BASE, mediaType, QString::number(tmdbId), apiKey(), appendTo);

    QNetworkReply* r = getJson(url);
    connect(r, &QNetworkReply::finished, this, [r, mediaType, tmdbId, this]() {
        r->deleteLater();
        if (r->error() != QNetworkReply::NoError) {
            emit scraperError("Detail fetch failed: " + r->errorString());
            return;
        }
        QJsonObject obj = QJsonDocument::fromJson(r->readAll()).object();
        TileData td = parseDetailsJson(obj, mediaType);

        // Backdrop
        QJsonArray backdrops = obj["images"].toObject()["backdrops"].toArray();
        if (!backdrops.isEmpty())
            downloadBackdrop(td.id, backdrops[0].toObject()["file_path"].toString());

        // For TV with an upcoming episode, check for multi-episode premiere
        if (mediaType == "tv" && !td.rawDateText.isEmpty() &&
            td.rawDateText.contains('|'))
        {
            QStringList parts = td.rawDateText.split('|');
            if (parts[0] == "SCAN") {
                // next_episode_to_air was empty — scan the season for future episodes
                int season = parts[1].toInt();
                fetchSeasonForFutureEp(tmdbId, season, td, false);
            } else {
                QString airDate = parts[0];
                int season      = parts[1].toInt();
                fetchSeasonForMultiEp(tmdbId, season, airDate, td, false);
            }
        } else {
            emit dataReady(td);
        }
    });
}

// =============================================================================
//  refreshTile — called on startup for each saved tile
//               Preserves existing id and user-set imagePath unless backdrop missing
// =============================================================================
void TmdbScraper::refreshTile(const TileData& existing)
{
    if (existing.tmdbId <= 0) return;

    QString appendTo = (existing.mediaType == "tv") ? "next_episode_to_air,images" : "release_dates,images";
    QString url = QString("%1/%2/%3?api_key=%4&append_to_response=%5")
        .arg(BASE, existing.mediaType, QString::number(existing.tmdbId), apiKey(), appendTo);

    QNetworkReply* r = getJson(url);
    connect(r, &QNetworkReply::finished, this, [r, existing, this]() {
        r->deleteLater();
        if (r->error() != QNetworkReply::NoError) {
            qDebug() << "[TmdbScraper] Refresh failed for" << existing.title;
            return;
        }
        QJsonObject obj = QJsonDocument::fromJson(r->readAll()).object();
        TileData updated = parseDetailsJson(obj, existing.mediaType,
                                            existing.id, existing.imagePath);

        // Always preserve user customisations — TMDB refresh must never overwrite these
        updated.customTitle   = existing.customTitle;
        updated.customDate    = existing.customDate;
        updated.customDateStr = existing.customDateStr;
        updated.customAirTime = existing.customAirTime;  // never overwrite user's time

        // If backdrop is missing or file was deleted, re-download
        bool backdropMissing = existing.imagePath.isEmpty() ||
                               !QFile::exists(existing.imagePath);
        if (backdropMissing) {
            QJsonArray backdrops = obj["images"].toObject()["backdrops"].toArray();
            if (!backdrops.isEmpty())
                downloadBackdrop(existing.id, backdrops[0].toObject()["file_path"].toString());
        }

        // Preserve notified flag unless date changed
        updated.notified = (existing.targetDate == updated.targetDate) && existing.notified;

        // For TV with upcoming episode, check for multi-episode premiere
        if (existing.mediaType == "tv" && !updated.rawDateText.isEmpty() &&
            updated.rawDateText.contains('|'))
        {
            QStringList parts = updated.rawDateText.split('|');
            QString airDate = parts[0];
            int season      = parts[1].toInt();
            if (parts[0] == "SCAN") {
                fetchSeasonForFutureEp(existing.tmdbId, season, updated, true);
            } else {
                fetchSeasonForMultiEp(existing.tmdbId, season, airDate, updated, true);
            }
        } else {
            emit tileRefreshed(updated);
        }
    });
}

// =============================================================================
//  fetchCreditsForResults — parallel credits for the drop-up
// =============================================================================
void TmdbScraper::fetchCreditsForResults(const QList<SearchResult>& results)
{
    for (const SearchResult& sr : results) {
        QString url = QString("%1/%2/%3/credits?api_key=%4")
            .arg(BASE, sr.mediaType, QString::number(sr.id), apiKey());
        int tmdbId    = sr.id;
        QString mtype = sr.mediaType;
        QNetworkReply* r = getJson(url);

        connect(r, &QNetworkReply::finished, this, [r, tmdbId, mtype, this]() {
            r->deleteLater();
            if (r->error() != QNetworkReply::NoError) return;
            QJsonObject obj  = QJsonDocument::fromJson(r->readAll()).object();
            QJsonArray  crew = obj["crew"].toArray();
            QJsonArray  cast = obj["cast"].toArray();

            QString director;
            QStringList dirJobs = (mtype == "movie")
                ? QStringList{"Director"}
                : QStringList{"Creator", "Series Creator", "Executive Producer"};
            for (const QJsonValue& v : crew) {
                QJsonObject c = v.toObject();
                if (dirJobs.contains(c["job"].toString())) {
                    director = c["name"].toString();
                    break;
                }
            }
            QStringList castNames;
            for (int i = 0; i < qMin(3, (int)cast.size()); ++i)
                castNames << cast[i].toObject()["name"].toString();

            emit creditsReady(tmdbId, director, castNames.join(", "));
        });
    }
}

// =============================================================================
void TmdbScraper::downloadBackdrop(const QString& tileId, const QString& backdropPath)
{
    QString url = QString("https://image.tmdb.org/t/p/w1280%1").arg(backdropPath);
    QNetworkReply* r = m_nam->get(QNetworkRequest(QUrl(url)));
    connect(r, &QNetworkReply::finished, this, [r, tileId, backdropPath, this]() {
        r->deleteLater();
        if (r->error() != QNetworkReply::NoError) return;
        QString dir = QStandardPaths::writableLocation(
                          QStandardPaths::AppDataLocation) + "/fetched_images";
        QDir().mkpath(dir);
        QString ext       = backdropPath.section('.', -1);
        QString localPath = dir + "/" + tileId + "." + ext;
        QFile f(localPath);
        if (f.open(QIODevice::WriteOnly)) {
            f.write(r->readAll());
            f.close();
            emit posterReady(tileId, localPath);
        }
    });
}

QNetworkReply* TmdbScraper::getJson(const QString& url)
{
    QNetworkRequest req{QUrl(url)};
    req.setRawHeader("Accept", "application/json");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    return m_nam->get(req);
}
