# CANITY

[简体中文](README.md) · **English**

[![CI](https://github.com/sakurapipi/CANITY/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/sakurapipi/CANITY/actions/workflows/ci.yml)

CANITY is an MIT-licensed, first-person pixel horror escape game written in C99. Explore floors, collect fuses, power exits, and decide when to extract. Enemy behavior responds to what you carry, how much noise you make, and where you shine your flashlight.

**Current stable release · v2.5.0**

- [Windows and Web downloads](https://github.com/sakurapipi/CANITY/releases/tag/v2.5.0)
- [Play in a desktop or mobile browser](https://sakurapipi.github.io/CANITY/)
- [Bilingual release notes and save migration](docs/RELEASE-v2.5.0.md)

## Play

- **Windows** · Extract the complete ZIP and open `canity.exe`. Keep the included `music/` folder beside it.
- **Browser** · Use the online link, or serve the extracted multi-file build with `python -m http.server 8080`. The standalone HTML embeds the game and music. Mobile browsers work best in landscape. Tap or click once to enable audio.
- **Controls** · WASD moves, Shift sprints, E interacts, F toggles the flashlight, and holding R cranks it. J opens the inventory and K drops gold. Hold Tab for the minimap or use M for the full map. On touch, hold the wrench button to prepare a swing and release to strike. The settings menu lists the current bindings.

## Modes and progression

- **Disorder Protocol** · The three B-17 prologue chapters feature the Technician, Archivist, and Security Guard. Finish the prologue to claim one free special character. Each other character can be unlocked separately for 10,000 bank gold, and direct purchase is available before finishing the story.
- **Classic and Endless** · Search for fuses and supplies. Four threat tiers change enemy behavior, and ten floor events offer different risks and rewards. The archive lists enemies, themed rooms, and events.
- **Campaign, Daily Challenge, and Training** · Play authored challenges, attempt the daily map, or practice interactions and tools. Daily objectives progress in the daily mode.
- **Progression** · Spend run gold on temporary upgrades and supplies. Spend bank gold on persistent mechanics through the Ghost, Raider, and Counter branches. The in-run skill tree opens details on one click and purchases on a double click.

## Saves

The stable release uses save format **v46**. Windows defaults to `%LOCALAPPDATA%/CANITY/saves/`. Set `CANITY_DATA_DIR` for an isolated save directory. Web storage belongs to the browser origin. Export or back up saves before clearing browser data or switching versions.

An in-progress run from the retired eight-floor story is archived before migration. The confirmation screen shows the archive filename. Confirm to retain your bank balance and profile unlocks. Use the RC3 release with a copy of the archive to finish the old route. A save written by a newer game version is left intact and can be reopened with the compatible version.

## Build and verify

Install Python 3 and MinGW-w64 GCC with `windres` for Windows, or Emscripten for Web. Source selection is managed by `tools/build_sources.py`.

```sh
python -X utf8 tools/build_validate.py
python -X utf8 tools/build_web.py
python -X utf8 tools/build_web.py --single
```

The Windows build is Lite by default. Pass `--embed-music` to embed the MP3 files. Browser output is written to `web/dist/`. The [GitHub Actions history](https://github.com/sakurapipi/CANITY/actions) records platform builds and the automated regression results. The Release manifest lists the commit and SHA-256 checksums.

## Source and license

Game logic and rendering live in `src/`, public interfaces in `include/`, and validation and build tooling in `tests/` and `tools/`. Platform backends live in `src/platform/`; Web assets and the browser shell live in `web/`. See the [module notes](docs/modules/README.md) and [Web build notes](docs/WEB.md).

The game code uses the [MIT License](LICENSE). The bundled Noto Sans SC glyph atlas uses the [SIL Open Font License](l10n/OFL-NotoSansSC.txt). Keep the included notices when redistributing game assets.
