# Launcher skin

`build/OpenRCT.exe` draws its menu in the style of the game's windows. Everything taken from the game
is read **at run time** from the user's folder (`game_dir`) after `check_game()` sets `game_ok`, and
reloaded when the folder changes. Nothing from the game is in the repository; the fallbacks drawn with
GDI and the exe icon are original art.

## Where the sprite index comes from

`CSG1.DAT` holds only pixels. The index (`G1Element`, 16 bytes each, `src/paint.h`) is a separate file:

- `0x41C810` (called once from the startup sequence at `0x42CEC4`) opens file #0 of the path table at
  `0x78CEE4` (`Data\CSG1.DAT`), allocates its size and reads it whole; the buffer goes to `[0x88B9B4]`.
- `0x41C937` opens file #1 (`Data\CSG1I.DAT`), reads `0x1111D0` bytes (`0x1111D` = 69 917 entries)
  straight into `g_g1_elements` (`0xAFC290`), then adds `[0x88B9B4]` to every `offset`. So `offset` in
  the file is relative to the start of `CSG1.DAT`.
- `0x41952B` builds the path of file #n: the directory is the install path (`0x88B064`) or, when the
  byte `0x88AF08[n]` is set, the CD path (`0x88B168`). File #1 is a "CD" file. Both paths come from
  `HKLM\SOFTWARE\WOW6432Node\Fish Technology Group\RollerCoaster Tycoon Setup` (`path`, `SetupPath`).
- On Steam, `SetupPath` is `<game>\RCTdeluxe_install`, and the index is
  **`RCTdeluxe_install\Data\csg1i.dat`** (1 118 672 bytes). It is not in `<game>\Data`.

The launcher tries `Data\CSG1I.DAT` (CD/GOG layouts) and then `RCTdeluxe_install\Data\csg1i.dat`. It is a
plain file, so the launcher reads it itself; there is no need to load the game image to get it.

## Sprite format (confirmed against the Steam files)

| flags | meaning |
|---|---|
| `1` | plain bitmap, `width*height` bytes |
| `4` (with `1`) | RLE per row: `uint16` offset per row, then segments `[len \| 0x80 = last][x][len bytes]` |
| `8` | palette: `x_offset` = first index, `width` = count, data in B,G,R triples |
| `16` | seen on some sprites (1 234 with `21`, 790 with `17`), irrelevant for drawing here |

Pixels are 8-bit palette indices; 0 is transparent. Text glyphs are drawn with index 1 and recoloured
at draw time.

## Sprite ids used (constants in `launcher/skin.c`)

| constant | id | what |
|---|---|---|
| `SPR_GRASS` | 1012 | flat grass tile, 32×15, tiled staggered like the map |
| `SPR_PALETTE` | 2024 | main palette, 236 colours from index 10 |
| `SPR_FONT` | 45845 + code | medium UI font, Latin-1 (accents included); glyph pixel 1 = face, 2 = baked drop shadow |
| `SPR_ICON_NEW` | 64507 | title menu "new game" icon, on "Jogar" |
| `SPR_ICON_EXIT` | 64510 | title menu "exit" icon, on "Sair" |
| `SPR_LOGO` | 64511 | "RollerCoaster Tycoon" logo, 192×108 (main palette) |

Other ids found on the way: 0–39 terrain at small zoom, 46069 + code tiny font, 46293 + code big font,
64506 tick mark (20×18, too big for an 11 px box; the checkbox tick is own art), 64508/64509 the other title
menu icons. The logo pieces at 64512+ (Chris Sawyer, Hasbro, MicroProse, 3D title) carry their own
palettes (64519/64523/64526/64533) and are not used.

`python tools/g1sheet.py <game dir> <first id> <count> [zoom]` writes a contact sheet to `build/` to find
more ids.

## Palette ramps

The palette is made of 12-shade ramps from index 10 (grey 10, olive 22, beige 34, yellow 46, maroon 58,
dark green 70, light green 82, green 94, salmon 106, violet 118, blue 130, teal 142, purple 154, red 166,
orange 178, dark teal 190, pink 202, brown 214). A ramp is named by its first index.

| use | ramp | shades |
|---|---|---|
| window body, panels, fields, "Sair" (`RAMP_MAIN`) | 22 olive | fill 7, light edge 10, dark edge 4, outline 1; inset: 2/10, fill 5 |
| title bar, "Jogar" (`RAMP_ACCENT`) | 58 maroon | same as above |
| status OK / error (`RAMP_OK`, `RAMP_ERROR`) | 94 green / 166 red | fill 5 |
| text | 10 grey | 11 (light), 0 (shadow) |

Without the game, `skin_color()` interpolates 12 shades between a dark and a light colour picked by hand
for each ramp (`own_ramps[]`), so the fallback look is original and nothing of the palette is committed.

**Changing the theme** is one line: `#define RAMP_MAIN 22` in `launcher/skin.h` (e.g. 214 for brown, 34 for
beige). A ramp without an entry in `own_ramps[]` falls back to grey when no game folder is set.

## Scale

Pixel art uses `k = max(1, round(dpi / 96))`; the layout stays in 96-dpi units (`S()`). Since `k` can be
up to 1.33× the layout scale (150 %: k = 2), every sprite and every text takes the largest whole scale
<= k that fits its box (`fit()` and `skin_text()`); nothing is ever resampled. Text that the game font cannot
draw (non-Latin-1 characters) uses Tahoma without antialiasing at the same scale.
