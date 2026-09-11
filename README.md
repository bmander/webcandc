# webcandc

The original 1995 *Command & Conquer* (Tiberian Dawn, Windows 95 edition) compiled to
WebAssembly and running in the browser.

The game code is EA's 2025 GPL release of the 1995 source, kept as close to the original as
possible: every change is a small, commented (`webcandc:`) diff against the imported files.
Windows 95 and DirectX are provided by a thin platform layer on SDL2 and Emscripten, so the
game's own window procedure, DirectDraw drawing, DirectSound mixing and VQA movie player run
largely as they did in 1995.

## What's in the tree

| Path | What it is |
|---|---|
| `src/game` | EA's Tiberian Dawn source (1995), imported unmodified, then patched |
| `src/wwlib` | Westwood's Win32 library, from the Remastered Collection release; stubs restored, assembly ported to C++, SOUNDIO from Red Alert's library |
| `src/vqa` | Westwood's WINVQ movie player (from the Red Alert release) |
| `src/platform` | Win32 messages/timers, DirectDraw, DirectSound, DOS file calls on SDL2/Emscripten; the data loader's installer unpacker |
| `src/compat` | Headers standing in for Watcom, Windows, COM and Greenleaf |
| `web` | The page shell (data loader) and the default `CONQUER.INI` |
| `tools` | Build script, headless test harness helpers, MIX and InstallShield tools |
| `reference` | Other source releases used as references (not built; not in git) |
| `docs` | Notes, including the Win32/DirectX surface survey |

See `PLAN.md` for the port plan, status and the lessons learned along the way.

## Game data

The game data is not included. EA released C&C Gold as freeware in 2007. The player build asks
for the GDI disc image (`CNC95_GDI.iso`, e.g. from
[CnC Communications Center](https://cnc-comm.com/command-and-conquer/downloads/the-game/gdi-disc-win95))
once, unpacks it in the browser (including the installer's `SETUP.Z`) and keeps the files in the
browser's IndexedDB. Save games are stored there too.

For development builds, put the data under `data/` (gitignored):

- `data/cd/` — the `.MIX` files: the disc's root `.MIX` files plus those inside `INSTALL/SETUP.Z`,
  which `tools/isz_extract.c` unpacks (`cc -o isz_extract tools/isz_extract.c blast.c`, with
  `blast.c`/`blast.h` from `src/platform`).
- `data/pkg/` — optional fallback files.

## Building

Requirements: Python 3 and the Emscripten SDK (`tools/emsdk`, gitignored).

```sh
git clone https://github.com/emscripten-core/emsdk tools/emsdk
tools/emsdk/emsdk install latest && tools/emsdk/emsdk activate latest
source tools/emsdk/emsdk_env.sh

python3 tools/build.py                      # debug build, data preloaded   -> build/debug/web
python3 tools/build.py --release --no-data  # player build, asks for data   -> build/release/web
python3 tools/build.py --headless           # node test harness             -> build/headless/web
```

Add `--with-movies` to package `MOVIES.MIX` into a data-preloading build. Serve a `web`
directory over HTTP (for example `python3 -m http.server`) and open `index.html`.

## The hosted site

`tools/deploy_pages.py` assembles `build/pages`: the player build plus the disc's data files
without the movies (about 65 MB, music included) and a `data/manifest.json` listing them. A
first-time visitor downloads those once into the browser's IndexedDB; the movies can be added
from their own disc image. `--push origin` publishes `build/pages` as a single force-pushed
commit on the `gh-pages` branch, so the game data never enters the source history.

```sh
python3 tools/build.py --release --no-data
python3 tools/deploy_pages.py --push origin
```

This is an unofficial project, not affiliated with or endorsed by Electronic Arts.

## Testing without a browser

The headless build runs the whole game under Node:

```sh
cd build/headless/web
WEBCANDC_FRAMES=/tmp/frames WEBCANDC_SCRIPT=/tmp/script.txt node --stack-size=4000 index.js
```

- `WEBCANDC_FRAMES` — directory for a PPM screen dump once a second.
- `WEBCANDC_SCRIPT` — timed input, one event per line: `9000 click 160 200`, `12000 key ESCAPE`,
  `15000 move 300 230`, `60000 exit` (milliseconds since start, game coordinates).
- `WEBCANDC_WAV` — capture the mixed audio to a WAV file.
- stderr shows every string drawn (`[text]`), missing files, and a call-stack trace every five
  seconds.

## Licence

GPL v3 with EA's additional terms (see `src/game/LICENSE.md`). `src/platform/blast.c` is
Mark Adler's zlib-licensed decompressor.
