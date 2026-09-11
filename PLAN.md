# webcandc — porting the original C&C (1995) source to WebAssembly

## Status (2026-09-11)

| Milestone | State |
|---|---|
| M1 everything compiles | done — 200 translation units, wasm32 |
| M2 links, runs headless | done — node harness (`tools/build.py --headless`, frame dumps, scripted input, stack traces) |
| M3 menu in the browser | done — side-selection screen in Chrome |
| M4 GDI mission 1, no sound | done — mission plays; selection/mouse work in Chrome |
| M5 audio | done — Westwood SOUNDIO on DirectSound-over-SDL (browser device path untested) |
| M6 movies | in progress — WINVQ player port |
| M7 ship | in progress — player build (`--release --no-data`) asks for the freeware disc image and keeps data in IndexedDB |

Things learned along the way (beyond the original risk list):
- The Remastered WIN32LIB had video mode, keyboard handler and mouse drawing stubbed out; restored from the 1995 bodies left under `#if 0`.
- Watcom enum sizing matters: game code is built with `-fshort-enums` (SDL-facing files excepted).
- Tiberian Dawn windows are in 8-pixel units; the Remastered GBUFFER.H assumed Red Alert's pixel units.
- The OpenRA data package lacks the Win95 fonts (UPDATE.MIX); the disc's INSTALL/SETUP.Z (InstallShield 3) holds them.

Known issues:
- Tooltip boxes lose their first glyph and leave remnants (map-cell refresh around help text).
- Nod-campaign movies live on the Nod disc (a second MOVIES.MIX); only the GDI disc is supported so far.
- Multiplayer is off (IPX/Winsock/DDE report no network).

**Approach B:** start from EA's original GPL release of Tiberian Dawn and keep it as the base.
Where Vanilla Conquer (VC) has already solved a problem (assembly rewrites, SDL backends,
decoders), use its code as a reference rather than inventing new solutions.
Every change stays a readable diff against the 1995 code.

## What we're starting from (verified)

| Source | Role | Notes |
|---|---|---|
| `electronicarts/CnC_Tiberian_Dawn` (2025) | **Game code (base)** | 156 .CPP / 133 .H, ~198k lines, 9 .ASM (7.8k lines). Watcom 10.6 + TASM/MASM. Doesn't build as released. |
| `electronicarts/CnC_Remastered_Collection` → `TIBERIANDAWN/WIN32LIB` | **Engine lib (base)** | 96 files, 4 .ASM. Version-matched to TD. Supplies most missing headers (`WWLIB32.H`, `WWMEM.H`, `TIMER.H`, `FILEPCX.H`, `MONO.H`, `WINCOMM.H`, `MODEMREG.H`). Probably **partial**: the Remaster renders outside the DLL, so drawing/shape code may be missing. |
| `electronicarts/CnC_Red_Alert` → `WIN32LIB`, `WINVQ` | Reference | Fuller (later) Westwood lib (~73 real .ASM) and the VQA movie player. Fills gaps. |
| `TheAssemblyArmada/Vanilla-Conquer` | Reference / crib | 0 asm; `common/` ~65k lines of C++ replacements, SDL2/OpenAL backends, VQA/AUD decoders. GPLv3 (compatible). clang-formatted, so borrow per function, not per file. |

Original compiler flags (from `MAKEFILE`): `wpp386 -j -s -bt=NT …`
- `-j` means signed `char`, which matches clang's default.
- There's no `-ei`, so Watcom used the smallest type that fits for enums. Clang uses `int`; see risks.

Code that must be replaced or removed:
- **Game assembly:** `KEYFBUFF` (shape blitter, 4.8k lines), `TXTPRNT`, `COORDA`, `SUPPORT`. Drop `MMX`, `PAGFAULT`, `IPXPROT`, `IPXREAL`, `WINASM`.
- **Engine-lib assembly:** `TOBUFF`, `REMAP`, `XORDELTA`, `FACINGFF`.
- **Watcom inline code:** `#pragma aux` in `COORD.CPP`, `DPMI.CPP`, `MONOC.CPP`, `JSHELL.H`.
- **Third-party libraries (not released):** DirectX 5, HMI SOS (audio), Greenleaf GCL (modem, 34 files).

## Target layout

```
webcandc/
  PLAN.md
  CMakeLists.txt
  src/game/        EA TD source — pristine import, then modified
  src/wwlib/       Remastered TD WIN32LIB — pristine import, then modified
  src/platform/    new: SDL2/Emscripten video, input, timer, audio, file backends
  src/compat/      watcom_compat.h and friends
  web/             HTML shell + loader JS
  tests/           unit/differential tests (run under Node)
  tools/           stub generator, helper scripts; emsdk (gitignored)
  reference/       RA, Remastered, VC clones — read-only, never compiled (gitignored)
```

## Key decisions

1. **Build for wasm first, no native 64-bit build.** The original code assumes 32-bit pointers and `long`, and so does wasm32.
   A 64-bit native build would mean redoing VC's pointer-truncation fixes. Debug with headless runs under Node,
   Chrome DevTools DWARF debugging, and Emscripten's ASan/UBSan.
2. **SDL2 via Emscripten's built-in port** for video, input and audio, rather than raw web APIs. This keeps a native build possible later.
3. **Single-player first.** IPX, modem, DDE and WChat code is compiled out behind `WEBCANDC_NO_NET`.
4. **Pristine imports first.** Commit the original sources unmodified, then normalize line endings in a separate commit, then make changes.

---

## Phases

### Phase 0: Setup (~½ day)
- `git init`. Commit 1: EA TD source, untouched. Commit 2: Remastered `TIBERIANDAWN/WIN32LIB`, untouched. Commit 3: CRLF→LF.
- Move the existing clones (`CnC_Tiberian_Dawn/`, `Vanilla-Conquer/`) under `reference/`. Add RA and Remastered clones there.
- Install a pinned emsdk into `tools/emsdk`.
- **You:** get the game data. C&C was made freeware by EA in 2007 (GDI and Nod discs); we need the MIX files (`CONQUER.MIX`, `GENERAL.MIX`, `LOCAL.MIX`, theaters, `SOUNDS.MIX`, …).
  Game data never goes into the repo or the deployed site.

**Exit:** repo with clean import history; `emcc --version` works.

### Phase 1: Make it compile (~1–2 weeks)
- CMake (`emcmake`), `-std=gnu++14` (the code uses `register`), `-fno-strict-aliasing -fwrapv -fsigned-char`. Start with warnings muted.
- `src/compat/watcom_compat.h`:
  - no-op the old calling-convention keywords `near/far/huge/_pascal/__cdecl/_loadds`
  - `min`/`max`, `stricmp`/`strcmpi`/`strupr`/`itoa`/`_splitpath`/`filelength`, `<dos.h>`/`<io.h>` shims
- Fix Watcom-isms by hand:
  - `for`-scope leaks (loop variables used after the loop)
  - implicit `int`, missing prototypes, `const` issues
  - `#pragma aux` → plain C (fixed-point coordinate math in `COORD.CPP`)
- Compile out DPMI, IPX/IPX95, Greenleaf/serial (`NULLDLG`, `WINCOMM`), DDE, TCPIP, MONOC, MEMCHECK.
- Handle include-name case (fine on macOS, but CI on Linux is case-sensitive).

**Exit:** every game and wwlib `.CPP` compiles to an object with `em++`.

### Phase 2: Link + replace assembly (~2–3 weeks; the hard part)
- Link, collect the undefined symbols, and have `tools/gen_stubs.py` generate `stubs.cpp`, where each stub logs its name once.
  **This produces the real list of missing engine code**, including whatever the Remastered WIN32LIB lacks.
- Port the assembly routines, starting with what the title screen needs:
  - wwlib: `TOBUFF` (buffer blits), `REMAP`, `XORDELTA` (WSA animation), `FACINGFF`,
    plus gaps filled from RA's WIN32LIB (`DRAWBUFF`, `SHAPE`, `MISC`): Fill_Rect, Draw_Line, Clear, To_Page, LCW_Uncompress, etc.
  - game: `COORDA`, `TXTPRNT` (text), `SUPPORT`, and `KEYFBUFF` (`Buffer_Frame_To_Page`).
    That last one is the renderer's core: ghost, shadow, fade, predator and transparency flags.
- Method:
  - Treat the original assembly as the spec. Port it, using VC's C++ equivalent as a crib where one exists.
  - Add **differential tests** in `tests/`: run our port and VC's version on the same fuzzed shapes and buffers under Node, and compare bytes.

**Exit:** the build links; runs headless under Node through init until the first platform call.

### Phase 3: Platform layer on SDL2 (~1–2 weeks)
Replace the Win32/DirectX parts of wwlib (`DDRAW.CPP`, `WINDOWS.CPP`, `KEYBOARD.CPP`, `MOUSEWW.CPP`, `TIMER*.CPP`) with `src/platform/`:
- **Video:**
  - Video surfaces become plain memory.
  - `Set_Video_Mode` (640×400×8) → SDL window/canvas.
  - Each frame: expand the 8-bit paletted buffer to RGBA and upload it as a texture.
  - Palette changes and fades trigger a re-present.
- **Mouse:** keep the original software cursor (`WWMouse`) drawn into the buffer. That's the most faithful option.
- **Keyboard/mouse input:** SDL events → `WWKeyboard` queue (map SDL keys to the VK_* codes the game expects).
- **Timer:** the Windows multimedia-timer thread (60 Hz tick counters) → counters computed from `SDL_GetTicks` (no threads needed).
- **Files:** `CDFileClass`/CD-drive detection → a virtual `/data` directory; MIX loading through POSIX `RawFileClass`.

**Exit (M3):** title screen and main menu render and respond in the browser.

### Phase 4: Browser runtime (~1 week, overlaps Phase 3)
- **Yielding:** the game spins in nested blocking loops (menus, dialogs, `Main_Loop`, movies).
  - Use **Asyncify**: yield (`emscripten_sleep(0)`) inside `Call_Back()`, which is called from 102 places including every wait loop, and on present.
  - Limit the overhead with `ASYNCIFY_ONLY`/`ADD` lists. Try `-sJSPI` as a lighter alternative.
- **Data:** a first-run page asks the user for their C&C files and stores them in IDBFS at `/data`.
  Saves go in `/data/save`, persisted via `FS.syncfs`.
- **Shell:** canvas with integer/nearest-neighbour scaling, fullscreen, audio unlocked on first click.

**Exit (M4):** GDI mission 1 playable start to finish in Chrome, Firefox and Safari, with no sound.

### Phase 5: Audio (~1 week)
- HMI SOS/DirectSound → an SDL2 audio-callback mixer.
- Port AUD decompression (Westwood ADPCM + IMA; RA `AUDIO` lib, VC `auduncmp.cpp`).
- Implement `Play_Sample` and `File_Stream_Sample` (music and EVA voice streamed from MIX files).

**Exit (M5):** sound effects, EVA voice and music work.

### Phase 6: Movies (~1 week)
- VQA player from RA's `WINVQ/VQA32` and `VQM32`, using VC's `vqa*` as a crib. Its codebook-unpacking assembly also needs porting.

**Exit (M6):** intro, briefings and cutscenes play.

### Phase 7: Polish & ship
- Save/load, options, both campaigns, win/lose/score screens.
- Performance: `-O3`, LTO, profile the blitters.
- Static hosting (e.g. GitHub Pages), with no game data served.
- Stretch goals: multiplayer over WebSocket/WebRTC behind the IPX manager interface; the internal map editor.

**Rough total: 2–3 months of focused work.** Phases 1–2 are about half of it.

---

## Risks

1. **The Remastered WIN32LIB is partial.** Gaps come from RA's later WIN32LIB, where the API may have drifted.
   The stub list in Phase 2 measures this early.
2. **Enum size.** Watcom made enums the smallest type that fits; clang makes them `int`.
   We don't need compatibility with original save files or network packets, so `int` is probably fine.
   Watch for code that relies on wrap-around or on `sizeof` an enum. `-fshort-enums` is the fallback, but it's risky with libc headers.
3. **Struct packing.** Structs that mirror file formats on disk (MIX, SHP, AUD, VQA headers, 2 existing `#pragma pack`s)
   need explicit `pack(1)`. Audit them against VC's definitions.
4. **Undefined behaviour in 1995 code** that modern optimizers exploit.
   Mitigations: `-fno-strict-aliasing -fwrapv`, start at `-O1`, run ASan/UBSan in debug builds.
5. **Asyncify cost** in code size and speed. Mitigations:
   - JSPI
   - or restructure only the menus and dialogs, since `Main_Loop` is already a one-frame-per-call function
6. **Bitfields (334 declarations).** Their layout may differ from Watcom's. This only matters if they're serialized as raw bytes; audit save/load.
7. **Filename case** in includes and data files.
8. **License:** GPLv3 plus EA's additional terms (see `LICENSE.md`). Keep attribution in a NOTICE file. Never distribute game data.

## Milestones
- **M1:** everything compiles
- **M2:** links and runs headless
- **M3:** menu works in the browser
- **M4:** GDI mission 1 playable, no sound
- **M5:** audio
- **M6:** movies
- **M7:** shipped
