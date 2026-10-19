# OpenRCT

**A function-by-function reconstruction in C of RollerCoaster Tycoon** (Deluxe, Chris Sawyer, 1999/2003), made to preserve the game.

Every function rewritten here is checked against the original. Both run in the same process, on the same inputs and the same memory, and must produce the same outputs, registers and side effects. The reconstructed functions then replace the original ones while you play.

> **You need your own copy of the game.** No game files, sprites, sounds or code from the original are in this repository or in the release. OpenRCT reads them from your installation at run time.

| | |
|---|---|
| Version | 0.1 |
| Reconstructed | 42 of ~16 360 functions ([progress](docs/PROGRESS.md)) |
| Supported game build | RollerCoaster Tycoon Deluxe, **Steam** (`RCT.EXE` SHA-256 `bdfebd64…aa05c76`) |
| Platform | Windows 10 / 11 (32-bit exe, runs on 64-bit Windows) |
| License | [MIT](LICENSE) (this code only) |

---

## Contents

1. [Features](#features)
2. [Playing](#playing)
3. [Launcher options](#launcher-options)
4. [In-game controls](#in-game-controls)
5. [Settings file](#settings-file)
6. [How it works](#how-it-works)
7. [Building from source](#building-from-source)
8. [Testing](#testing)
9. [Reconstructing a function](#reconstructing-a-function)
10. [Repository layout](#repository-layout)
11. [Documentation](#documentation)
12. [Troubleshooting](#troubleshooting)
13. [FAQ](#faq)
14. [Legal](#legal)
15. [Credits](#credits)

---

## Features

**Reconstruction**
- **Verified code:** 42 functions of the original game are rewritten in C and checked differentially against the original. Among them:
  - the SV4/SC4 save and scenario reader and writer, with checksums (the whole module is done);
  - metal and wooden supports;
  - viewport rendering and window zoom;
  - the paint system: sprite primitives in all four rotations, depth sorting and drawing.
- **Exact interfaces:** the reconstructed code is called by the original asm, so each function also honours the original's register contract (which registers it preserves, returns and leaves behind).
- **Switchable:** the "use reconstructed code" option can be turned off to run the pure original for comparison.

**Modern display** (on by default, [details](docs/display.md))
- **Any window size and aspect ratio:** widescreen, ultrawide or portrait. The game area grows with the window instead of being stretched.
- **Borderless fullscreen** on any monitor (F11 or Alt+Enter).
- **Dynamic pixel zoom:** automatic (about 600 game pixels high) or fixed from 1× to 6×, always nearest-neighbour, so pixels stay sharp. Per-monitor DPI aware.
- **Map zoom past the original's closest level:** the mouse wheel zooms the map up to 2× and 4× closer, with picking (clicking on things) still exact.
- **Monitor changes are followed:** adding a monitor or changing its resolution while playing is handled.

**Launcher**
- **Styled menu** in the look of the game's own windows, drawn with your installation's sprites, palette and font. A built-in fallback is used until the game folder is found.
- **Game check:** the Steam folder is found automatically and `RCT.EXE` is checked by SHA-256 before starting.
- **Open saves directly:** pass a saved game or scenario on the command line (`--open`) or drag it onto the exe.
- **Portable:** a single exe with no installer and no DLLs beyond Windows'. The settings file sits next to it.

---

## Playing

### From a release

1. Install **RollerCoaster Tycoon Deluxe** from Steam.
2. Download `OpenRCT-v0.1-windows-portable-x86.zip` from the [Releases](https://github.com/erichs890/OpenRCT/releases) page and extract it anywhere. You get:
   ```
   openrct.exe       the launcher (and the whole engine)
   readme.txt        quick guide
   changelog.txt
   contributors.md
   licence.txt
   PRIVACY.md
   ```
3. Run `openrct.exe`. The menu opens. If your game is in the default Steam folder it is already filled in, otherwise use **Procurar…** to choose the folder that contains `RCT.EXE`:
   ```
   C:\Program Files (x86)\Steam\steamapps\common\RollerCoaster Tycoon Deluxe
   ```
4. The status line turns green (`OK: Steam (RCT Deluxe, 2003-03-12)`). Click **Jogar**.

Nothing is installed and nothing is written to the game folder other than the game's own saves. To uninstall, delete the folder.

### From source

See [Building from source](#building-from-source). After `make launcher`, run `build/OpenRCT.exe`.

---

## Launcher options

```
openrct.exe [--play] [--no-hooks] [--classic] [--dir <game folder>] [--open <file.SV4|file.SC4>]
openrct.exe <file.SV4|file.SC4>
```

| Option | Effect |
|---|---|
| *(none)* | Show the menu |
| `--play` | Skip the menu and start with the saved settings. It falls back to the menu if the game folder is not valid |
| `--dir <folder>` | Game folder to use (the folder containing `RCT.EXE`) |
| `--open <file>` | Start the game straight into this saved game (`.SV4`) or scenario (`.SC4`) |
| `<file>` | Same as `--play --open <file>`. This is what happens when you drop a file on the exe |
| `--no-hooks` | Run the original code only, with no reconstructed functions |
| `--classic` | Original 640×480 display, without the modern display layer |

The menu has the same choices:
- **Usar código reconstruído:** off is the same as `--no-hooks`.
- **Tela moderna:** off is the same as `--classic`.
- **Tela cheia:** start in borderless fullscreen.
- **Zoom:** automatic or a fixed pixel zoom.

---

## In-game controls

Everything from the original game works as before. Added by the modern display:

| Input | Action |
|---|---|
| Mouse wheel | Map zoom, now including 2× and 4× closer than the original's closest level |
| Ctrl + wheel, Ctrl + `+` / `−` | Pixel zoom (size of the whole picture) |
| Ctrl + `0` | Pixel zoom back to automatic |
| F11 or Alt + Enter | Toggle borderless fullscreen |

---

## Settings file

`OpenRCT.ini` is created next to the exe. It is written by the menu and can be edited by hand.

```ini
[game]
dir=C:\Program Files (x86)\Steam\steamapps\common\RollerCoaster Tycoon Deluxe
reconstructed=1        ; 0 = original code only

[display]
modern=1               ; 0 = original 640x480 display
fullscreen=0
zoom=0                 ; 0 = automatic, or 1, 1.25, 1.5, 1.75, 2, 2.5, 3, 3.5, 4, 5, 6
maximized=0            ; window state, saved on exit
x=… y=… w=… h=…        ; window position and size, saved on exit
```

---

## How it works

```
openrct.exe
 │
 ├─ 1. menu: find the game folder, check RCT.EXE (SHA-256)
 │
 ├─ 2. load RCT.EXE into its own address range (0x401000–0xC50000)
 │      the Steam exe is wrapped by ExeLock/NeoLite: it is decompressed
 │      in place, its imports bound to the real Windows DLLs
 │
 ├─ 3. install hooks: the first bytes of each reconstructed function
 │      become a jmp to this repository's C code (through a small asm
 │      adapter that keeps the original register contract)
 │
 ├─ 4. modern display: replace the window creation, the present step
 │      and the mouse mapping (through the import table and a few patches)
 │
 └─ 5. jump to the game's own entry point: from here on, it is the
        original game, running partly on reconstructed code
```

- **One process, one memory layout:** OpenRCT's own code is linked at `0xC50000`, above the original image. The game's globals are at their original addresses, and the C code uses them directly: `runtime/globals.ld` binds each `extern` to its address. Original and reconstruction share the same memory, so they can be swapped one function at a time.
- **No game code in the repository:** the original image is loaded from your `RCT.EXE` at run time. The repository contains only reconstructed C, the loader and tools.
- **Two kinds of original code:** most of the game is hand-written x86 assembly (MASM). A smaller part is C compiled with MSVC 6, plus its runtime library. The asm functions are reconstructed for **identical behaviour**, registers included. For the MSVC 6 C functions, identical bytes are also a goal, which needs the original compiler ([inventory](docs/inventory.md)).

---

## Building from source

### Requirements

| Tool | Used for |
|---|---|
| [MSYS2](https://www.msys2.org/) with the **mingw32** toolchain | building (the game is 32-bit, so everything is 32-bit) |
| Python 3.10+ with `tools/requirements.txt` (`pefile`, `unicorn`, `capstone`) | unpacking the exe and analysis tools (`tools/g1sheet.py` also uses Pillow) |
| Your Steam copy of RCT Deluxe | running the game and the tests |
| *(optional)* Ghidra and REA | reverse engineering new functions |

Install the toolchain from an MSYS2 shell:

```sh
pacman -S --needed mingw-w64-i686-gcc make
```

The Makefile calls `/c/msys64/mingw32/bin/gcc` directly. If MSYS2 is installed elsewhere, change `CC` at the top of the [Makefile](Makefile).

### Build

```sh
git clone https://github.com/erichs890/OpenRCT.git
cd OpenRCT
make launcher          # build/OpenRCT.exe
build/OpenRCT.exe
```

### Portable package

```sh
make dist              # dist/OpenRCT-v<VERSION>-windows-portable-x86/ and .zip
```

The package texts live in [launcher/dist/](launcher/dist/). The version is `VERSION` at the top of the Makefile. Each release adds an entry to [launcher/dist/changelog.txt](launcher/dist/changelog.txt).

---

## Testing

The tests run the original functions, so they need the game image. Prepare it once:

```sh
mkdir original
cp "/c/Program Files (x86)/Steam/steamapps/common/RollerCoaster Tycoon Deluxe/RCT.EXE" original/
pip install -r tools/requirements.txt
python tools/unpack.py original/RCT.EXE original/RCT.unpacked.exe
```

`original/` is in `.gitignore` and must never be committed. Then:

```sh
make test         # every test, plain and with all hooks installed (a few minutes)
make test-hooks   # every test once per hook, so each adapter runs under the original callers (~10 min)
```

The save/load tests also read the real saves and scenarios of your installation. Set `RCT_DIR` if the game is not in the default Steam folder.

Each test is **differential**:
1. reset the shared memory to an identical pseudo-random state;
2. run the original and snapshot everything;
3. reset again, run the reconstruction, snapshot again;
4. compare the outputs, the registers and flags, and the whole writable image (3.8 MB).

Tests are also checked by mutation: the reconstruction is broken on purpose and the test must catch it. See [docs/harness.md](docs/harness.md).

---

## Reconstructing a function

The workflow used for every function in [docs/functions/](docs/functions/):

1. **Pick:** choose a function from the attack order in [docs/inventory.md](docs/inventory.md). Leaf functions and the most-called ones come first.
2. **Read:** disassemble the original (Ghidra/REA, or capstone on `original/RCT.unpacked.exe`). Find what it reads and writes, and its register contract.
3. **Check what callers see:** `python tools/regflow.py` shows which registers the callers read after the call, which decides how exact the adapter has to be.
4. **Write the C:** put it in `src/`, with globals declared `extern` and bound in `runtime/globals.ld`.
5. **Adapter and hook:** add the register adapter in `runtime/hooks.S` and the entry in `rct_hooks[]` in `runtime/hooks.c`. Bump `HOOKS` in the Makefile.
6. **Test:** write `tests/test_<module>.c`, comparing against the original over thousands of random cases, then run a mutation check. `make test` and `make test-hooks` must pass.
7. **Try it in game:** run the title screen and a real park with all hooks.
8. **Document:** add `docs/functions/<ADDR>_<name>.md` (original address, proposed name, behaviour, evidence, verification) and a row in [docs/PROGRESS.md](docs/PROGRESS.md).

OpenRCT2 is consulted **only** as a reference for data structures and file formats. No code is copied from it.

---

## Repository layout

```
OpenRCT/
├── src/            reconstructed game code (one file per module)
│   ├── platform_file.c     file seek/read/write          (MSVC 6 C part)
│   ├── platform_display.c  screen invalidation           (MSVC 6 C part)
│   ├── sawyer_io.c         SV4/SC4 RLE streams and checksums
│   ├── paint_add.c         paint primitives (parent/child/attached, 4 rotations)
│   ├── paint_list.c        paint session, depth sort, draw
│   ├── paint_supports.c    metal and wooden supports
│   └── viewport.c          viewport render, window zoom, map magnification
├── runtime/        loads RCT.EXE in-process and connects the reconstruction
│   ├── image.c             loader and in-place decompression, rct_call()
│   ├── image_space.S       reserves the original address range
│   ├── hooks.c / hooks.S   hook table and register adapters
│   ├── globals.ld          C globals → original addresses
│   └── display.c           modern display layer
├── launcher/       openrct.exe: menu, skin, icon, package texts (dist/)
├── harness/        test runtime (load the image, random state, CHECK)
├── tests/          differential tests, one per module
├── tools/          unpacker, Ghidra seeding, inventory, regflow, peek, sprite sheet
├── docs/           methodology, subsystem map, progress, one doc per function
└── original/       YOUR game files (ignored by git, never committed)
```

---

## Documentation

| Document | What's in it |
|---|---|
| [docs/PROGRESS.md](docs/PROGRESS.md) | Every reconstructed function, how it was verified, link to its doc |
| [docs/functions/](docs/functions/) | One page per function: address, name, behaviour, evidence, verification |
| [docs/inventory.md](docs/inventory.md) | Exe layout, compilers, subsystem map and attack order |
| [docs/unpacking.md](docs/unpacking.md) | The ExeLock/NeoLite wrapper and how it is removed |
| [docs/harness.md](docs/harness.md) | Differential test harness, conventions, `regflow` |
| [docs/display.md](docs/display.md) | Modern display layer and map magnification |
| [docs/launcher.md](docs/launcher.md) | Launcher skin: sprite index, sprite format, ids, palette ramps |

---

## Troubleshooting

| Symptom | Fix |
|---|---|
| "RCT.EXE não encontrado nesta pasta" | Choose the folder that **contains** `RCT.EXE`, not its parent |
| "Data\CSG1.DAT não encontrado" | The installation is incomplete. Verify the game files in Steam (Properties → Installed Files → Verify) |
| "Versão de RCT.EXE não suportada" | Only the Steam build is supported for now (the reconstruction is tied to its addresses). A modified or other-store `RCT.EXE` will not start |
| The game looks wrong or crashes in some situation | Try again with **Usar código reconstruído** off (`--no-hooks`). If the original works, it's a reconstruction bug: please [open an issue](https://github.com/erichs890/OpenRCT/issues) with the save file |
| Display problems | Try `--classic` to rule out the modern display |
| Settings got into a bad state | Delete `OpenRCT.ini` next to the exe |
| Antivirus flags the exe | It loads and patches another exe in memory, which some heuristics dislike. Build it yourself from source if in doubt |

---

## FAQ

**Is this a port, like OpenRCT2?**
Not yet. OpenRCT2 is a full re-implementation of RCT2. OpenRCT is a step-by-step reconstruction of RCT1 that runs *inside* the original. Each function is proven identical before it replaces the original one. The long-term goal is to reconstruct everything, so that the original exe is no longer needed for its code (the game's data files always will be).

**Why does it need the original game?**
Most of the game is not reconstructed yet, and the graphics, sounds and scenarios are copyrighted and are not distributed here.

**Will GOG or CD versions work?**
Not yet. The reconstruction is tied to the addresses of the Steam build, which the launcher checks by SHA-256. Supporting more builds means mapping their addresses.

**Do my saves work?**
Yes. They are the game's normal `.SV4` files in the game's `Saved Games` folder, and the save/load code is reconstructed and verified against the original.

**Does it connect to the internet?**
No. See [PRIVACY.md](launcher/dist/PRIVACY.md).

---

## Legal

OpenRCT is an unofficial fan project for preservation. It is not affiliated with or endorsed by Chris Sawyer, Atari, Hasbro, MicroProse or Valve. *RollerCoaster Tycoon* is a trademark of its respective owners.

This repository contains only original code and documentation. The game's executable, graphics, sounds and scenarios are not included and must come from a legally owned copy. Do not open issues or pull requests that contain game files.

## Credits

- **Chris Sawyer**: RollerCoaster Tycoon.
- **Erich Schlaepfer** ([erichs890](https://github.com/erichs890)): OpenRCT.
- The [OpenRCT2](https://github.com/OpenRCT2/OpenRCT2) project, whose documentation of file formats and data structures was a valuable reference.

Licensed under the [MIT License](LICENSE).
