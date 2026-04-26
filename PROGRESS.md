# CineCountdown — Project Progress & Technical Reference

## Overview
CineCountdown is a **Qt 6 + CMake C++** desktop app for Windows that tracks upcoming
movie and TV show release dates as visual 16:9 tiles.  It is the successor to
EventCountdown (https://github.com/HijackAssassin/EventCountdown).

Two executables are produced:
- **CineCountdown.exe** — main tile manager (Qt Widgets)
- **CineCountdownTray.exe** — background notification daemon (system tray)

---

## Architecture

```
CineCountdown/
├── CMakeLists.txt              ← root workspace (open this in Qt Creator)
├── shared/
│   └── tiledata.h              ← TileData struct shared by both apps
├── MainApp/
│   ├── CMakeLists.txt
│   └── src/
│       ├── main.cpp            ← Qt6 app entry, showMaximized()
│       ├── mainwindow.*        ← main window, tile grid, search bar
│       ├── tilewidget.*        ← individual 16:9 tile (two transparent overlays)
│       ├── outlinedlabel.h     ← custom QLabel that paints black text outline
│       ├── edittiledialog.*    ← left-click edit menu (title/date/time/image)
│       ├── tmdbscraper.*       ← TMDB API client (search + details + backdrop)
│       ├── jsonmanager.*       ← reads/writes %APPDATA%\CineCountdown\tiles.json
│       └── addtiledialog.*     ← unused (search built into main window bar)
└── TrayApp/
    ├── CMakeLists.txt
    └── src/
        ├── main.cpp
        └── trayapp.*           ← system tray icon, 60s poll, IPC socket
```

---

## Building

**Prerequisites:** Qt 6.x (tested on 6.7.3 MinGW), CMake 3.21+, Ninja

**In Qt Creator:**
1. File → Open → select `CineCountdown/CMakeLists.txt` (root)
2. Select Desktop Qt 6 kit
3. Ctrl+B to build

**CMake note:** The `.rc` icon lines are commented out — to add a custom icon
place `app.ico` in `MainApp/resources/` and uncomment in `MainApp/CMakeLists.txt`.

---

## Data Storage

**tiles.json** — `%APPDATA%\CineCountdown\tiles.json`
**Backdrops**  — `%APPDATA%\CineCountdown\backdrops\<tileId>.jpg`

### TileData fields (tiledata.h)
| Field | Purpose |
|---|---|
| `id` | UUID, links tile to its backdrop file |
| `title` | TMDB-generated e.g. "Invincible (2021) • 4 Seasons" |
| `customTitle` | User override display name (empty = use title) |
| `tmdbId` | TMDB numeric ID — used to refresh on startup |
| `mediaType` | "movie" or "tv" |
| `targetDate` | TMDB release date (QDate) |
| `customDate` | User override date (invalid = use targetDate) |
| `customDateStr` | Human display of customDate |
| `customAirTime` | User-set release time (invalid = midnight) |
| `airTime` | TMDB air_time field if provided |
| `statusLabel` | e.g. "S02E01", "S02E01+E02", "Releases", "Released", "Last Episode" |
| `dateDisplay` | Human-readable TMDB date |
| `imagePath` | Absolute path to backdrop (empty = black tile) |
| `notified` | Tray app already fired notification for this tile |

**Effective values:**
- `displayTitle()` → customTitle if set, else title
- `displayDate()` → customDateStr if set, else dateDisplay
- `effectiveDate()` → customDate if valid, else targetDate
- `effectiveTime()` → customAirTime if valid, else airTime

---

## TMDB API

**Key:** `693ee361bc407ffa8973abcd76d80120`
**Base URL:** `https://api.themoviedb.org/3`
**Image base:** `https://image.tmdb.org/t/p/w1280`

### Flow
1. **Search:** `/search/multi?query=NAME` (2 pages in parallel)
2. **Details:** `/movie/ID?append_to_response=release_dates,images`
   or `/tv/ID?append_to_response=next_episode_to_air,images`
3. **Season check:** `/tv/ID/season/N` — detects double-episode premieres
4. **Credits:** `/movie/ID/credits` or `/tv/ID/credits` — for drop-up picker display

### Date logic (movies)
TMDB `release_dates` → look for US entry → prefer type 3 (Theatrical) → type 2 → type 4 → global fallback.
If system timezone is `America/*`, subtract 1 day for US Thursday preview screenings.

### Season count
Iterates `seasons[]` array and counts entries where `season_number > 0` (excludes Season 0 = Specials).

### All-caps title fix
If `title == title.toUpper()` and has 2+ capital letters, converts to Title Case
with common minor words ("a", "the", "of", etc.) staying lowercase.

---

## Tile Layout

Each tile is a `QWidget` with one child: `m_imageContainer` (16:9, full width).
Inside the container:
- `m_imageLabel` — backdrop, fills the container
- `m_bottomOverlay` (`OutlinedLabel`) — title, pinned to **top** of image
- `m_topOverlay` (`OutlinedLabel`) — countdown, pinned to **bottom** of image

`OutlinedLabel` is a custom `QLabel` subclass that overrides `paintEvent` to
draw text at 8 surrounding 1px offsets in black before drawing the white text on
top — producing a crisp readable outline against any backdrop.

Font sizes start at `BASE_PT = 21pt` and auto-shrink per-overlay until text fits.

---

## Countdown Format

**Active TV:**   `S02E01 - April 22, 2026 - 45d : 03h : 22m : 10s`
**Double ep:**   `S02E01+E02 - April 22, 2026 - 45d : 03h : 22m : 10s`
**Active movie:** `June 26, 2026 - 45d : 03h : 22m : 10s`
**Expired TV:**  `Last Episode - February 19, 2025`
**Expired movie:** `Released - June 26, 2022`
**No date:**     `No Release Date`

---

## Search Behaviour

- Fetches 2 pages from TMDB (`/search/multi`) in parallel
- Diacritic stripping: "Shogun" matches "Shōgun" (NormalizationForm_D)
- Dash normalisation: "Spiderman" matches "Spider-Man", "Xmen" matches "X-Men"
- Word-boundary matching: "The Boys" matches "The Boys" and "The Boys Reborn"
  but NOT "The Hardy Boys"
- Compact matching (fallback): strips all non-alphanumeric and uses `startsWith`
- Podcast filter: any result whose title contains "podcast" is excluded
- US origin_country results sorted before non-US; within each group: newest year first
- No-year results sorted last
- Year filter: typing `Superman 2025` strips the year and filters results to 2025 only
- Capped at 15 results shown in the drop-up picker
- Drop-up picker shows 3 rows, scrolls for more; fills in director + cast asynchronously

---

## Edit Dialog (left-click tile)

Fields:
- **Display Name** — custom display name (Reset = restore TMDB title)
- **Release Date** — custom date picker (Reset = restore TMDB date)
- **Release Time** — hour (1–12) + AM/PM; Reset sets to midnight (= no override)
- **Backdrop Image** — 16:9 preview; Select Image or Reset Image (re-downloads from TMDB)
- **Remove Tile** (red), **Cancel**, **Save**

Right-clicking a tile shows only "Remove Tile".

Any Reset button triggers a full TMDB refetch of that tile on Save.

---

## Startup Refresh

On every launch, `refreshAllTiles()` calls `refreshTile(td)` for each tile
with a valid `tmdbId`. This updates: title, statusLabel, targetDate, seasons.

**Preserved across refresh:** customTitle, customDate, customDateStr,
customAirTime, imagePath (unless missing/deleted → re-downloads backdrop).

**Title change detection:** if TMDB changes the base title
(e.g. "Superman: Legacy" → "Superman"), `customTitle` is cleared so the tile
picks up the new name automatically.

---

## Backdrop File Management

- Backdrops stored in `%APPDATA%\CineCountdown\backdrops\<tileId>.<ext>`
- Deleted when: tile removed; image replaced by user; Reset Image pressed;
  new backdrop downloaded for same tile (onPosterReady deletes old first)
- `cleanupOrphanedBackdrops()` runs on startup — scans the backdrops folder
  and deletes any file not referenced by a current tile

---

## Tile Sort Order

1. Active countdowns (has future date) — soonest first
2. Expired/ended — most recent first
3. No release date — at the very bottom

---

## IPC (main app ↔ tray app)

Named local socket: `CineCountdownTray`
Message: `REFRESH\n`

Main app sends after every save. Tray app reloads `tiles.json` on receipt.
If tray app is not running, write fails silently.

---

## Tray App

- Polls tiles every 60 seconds
- Fires a Windows `QSystemTrayIcon::showMessage` notification when any tile's
  countdown reaches zero
- Sets `notified = true` on the tile so it doesn't re-fire
- Double-click tray icon → launches `CineCountdown.exe` from same directory
- Context menu: Open Manager / Refresh / Quit
- `app.setQuitOnLastWindowClosed(false)` keeps it alive with no windows

---

## Known Limitations

- TMDB free tier does not provide per-episode air times in the base details call;
  `air_time` on the show object is inconsistently populated
- US early-screening -1 day applies to all movies for US timezones
- No drag-to-reorder tiles (sort is automatic)
- No custom tile creation (no-TMDB tiles) — planned for future

---

## Planned / Future

- Custom tiles with manually entered dates (no TMDB)
- Episode-level tracking per season
- Drag-to-reorder
- Auto re-scrape on startup already implemented; potential for periodic background refresh

---

## Key Files Changed Per Feature (most recent last)

| Feature | Files |
|---|---|
| Qt6+CMake setup | CMakeLists.txt (root + MainApp + TrayApp) |
| Core data model | shared/tiledata.h, jsonmanager.* |
| TMDB scraping | tmdbscraper.* |
| Tile display | tilewidget.*, outlinedlabel.h |
| Main window | mainwindow.* |
| Edit dialog | edittiledialog.* |
| Tray notifications | TrayApp/src/trayapp.* |

---

## Recent Fixes (latest session)

- **Air time removed from TMDB:** `td.airTime` is never set from TMDB data.
  All countdowns default to midnight. Users set times manually via edit dialog.
- **Season count:** Only counts seasons where `season_number > 0` AND
  `air_date` is set AND `air_date <= today`. Excludes Season 0 (Specials) and
  future unannounced seasons that TMDB lists speculatively.
- **Custom images copied to app folder:** When user picks an image via
  "Select Image…", it is copied to `%APPDATA%\CineCountdown\images\<id>_custom.<ext>`.
  The original file can be deleted — the app has its own copy.
- **Owned-path check unified:** All deletion logic uses `isOwned()` which checks
  for both `/backdrops/` and `/images/` paths.
- **`cleanupOrphanedBackdrops`** now cleans both `backdrops/` and `images/` folders.
