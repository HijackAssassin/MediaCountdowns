# 🎬 CineCountdown

**The successor to EventCountdown** — tile-based media release countdown for Windows.  
Built with **C++ · Qt 6 · CMake**.

---

## What it does

- Paste an **IMDB URL** into the bar at the bottom → click **Add Tile**
- The app scrapes the title and release date automatically
- A **16:9 tile** appears with the title above and date info below
- **Click any tile image** to assign a custom poster/banner
- **Hover the bottom bar** of a tile → live countdown ticks every second:  
  `234 days : 15 hours : 02 minutes : 40 seconds`
- Once a date has passed the hover countdown is disabled — date stays shown
- **Right-click** a tile → Remove Tile
- A **tray app** runs in the background and fires a Windows desktop  
  notification the moment any countdown reaches zero

---

## Architecture

```
CineCountdown/
│
├── CMakeLists.txt              ← Root workspace (open this in Qt Creator)
├── build.bat                   ← CLI build (Qt 6 + CMake + Ninja)
├── deploy.bat                  ← windeployqt6 packaging
│
├── shared/
│   └── tiledata.h              ← Core data model shared by both apps
│
├── MainApp/                    ← CineCountdown.exe  (tile manager)
│   ├── CMakeLists.txt
│   ├── resources/
│   │   └── app.rc              ← Windows icon resource
│   └── src/
│       ├── main.cpp
│       ├── mainwindow.*        ← Window with tile grid + bottom URL bar
│       ├── tilewidget.*        ← Individual 16:9 tile card
│       ├── imdfscraper.*       ← Async IMDB HTML scraper
│       ├── jsonmanager.*       ← Reads/writes tiles.json
│       └── addtiledialog.*     ← (reserved for future use)
│
└── TrayApp/                    ← CineCountdownTray.exe  (background daemon)
    ├── CMakeLists.txt
    └── src/
        ├── main.cpp
        └── trayapp.*           ← Tray icon + 60 s poll + IPC socket
```

---

## Data storage

All tile data is stored in:
```
%APPDATA%\CineCountdown\tiles.json
```

### tiles.json format

```json
[
  {
    "id":          "a1b2c3d4-...",
    "title":       "Invincible",
    "imdbUrl":     "https://www.imdb.com/title/tt6741278/",
    "statusLabel": "Next episode",
    "rawDateText": "April 22, 2026",
    "dateDisplay": "April 22, 2026",
    "targetDate":  "2026-04-22",
    "imagePath":   "C:/Users/You/Pictures/invincible.jpg",
    "notified":    false
  }
]
```

---

## IMDB scraping

| Field        | IMDB `data-testid` attribute |
|---|---|
| Title        | `hero__primary-text` |
| Status label | `tm-box-up-title` (e.g. "Next episode", "Pre-production") |
| Date         | `tm-box-up-date` (e.g. "Expected July 9, 2027") |

The date parser strips any prefix words ("Expected", "Coming soon", etc.)  
and finds `Month DD, YYYY` anywhere in the string using a regex.

**Bottom bar normal display:**  `Next episode - April 22, 2026`  
**Bottom bar hover display:**   `234 days : 15 hours : 02 minutes : 40 seconds`

---

## IPC between apps

The main app sends `REFRESH\n` to a named local socket called `CineCountdownTray`  
whenever it saves tiles. The tray app reloads `tiles.json` and re-syncs  
notification state. If the tray isn't running, the write fails silently.

---

## Building

### Prerequisites

| Tool | Where to get it |
|---|---|
| Qt 6.x (MinGW or MSVC) | https://www.qt.io/download-open-source |
| CMake 3.21+ | https://cmake.org/download |
| Ninja | https://ninja-build.org — or use MinGW Makefiles |
| Qt Creator (optional) | bundled with Qt installer |

### Option A — Qt Creator (easiest)

1. Open `CMakeLists.txt` (the root one) in Qt Creator
2. Select a **Qt 6** Desktop kit when prompted
3. Click **Build** → both executables are produced

### Option B — Command line

```bat
REM Optional: tell the script where Qt 6 is
set QT6_DIR=C:\Qt\6.7.0\mingw_64

build.bat          ← Release build
build.bat debug    ← Debug build
build.bat clean    ← Wipe build\ folder
```

### Packaging for distribution

```bat
deploy.bat
```

Produces a self-contained `dist\` folder with both `.exe` files and all Qt DLLs.

---

## Running

1. Start **`CineCountdownTray.exe`** first — it goes to the system tray silently
2. Start **`CineCountdown.exe`** — the tile manager window opens

To make the tray app start automatically on login, create a shortcut to  
`CineCountdownTray.exe` and place it in:
```
%APPDATA%\Microsoft\Windows\Start Menu\Programs\Startup\
```

---

## Fixes over EventCountdown

| Old problem | New solution |
|---|---|
| Manual date entry | Auto-scraped from IMDB |
| Wikipedia inconsistency | IMDB `data-testid` is reliable and always updated |
| Notification misfiring from bool flag | `notified` is set once, never re-fires |
| Long QTimer durations (days in ms) | 60-second poll loop — always accurate |
| No separation of status vs date | `statusLabel` and `dateDisplay` stored separately |

---

## Future roadmap

- Custom tiles with manually entered dates (no IMDB URL required)
- Episode-level tracking per season
- Tile reordering via drag and drop
- Auto re-scrape on startup to catch date changes announced after tile creation
