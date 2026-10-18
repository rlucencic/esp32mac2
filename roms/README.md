# Macintosh ROM images

Put your own ROM dumps here (they are copyrighted and not part of this
repository). The emulator recognises them by their checksum, so only these
files work:

| File | Machine | Size | Checksum |
|---|---|---|---|
| `lc.rom` | Macintosh LC | 512 KB | 350EACF0 |
| `ii.rom` | Macintosh II (the second, 800K ROM) | 256 KB | 9779D2C4 |
| `mdc824.rom` | Macintosh Display Card 8•24 (341-0868), the Mac II's video card | 32 KB | its format block |

`idf.py flash` writes them to the `rom_lc`, `rom_ii` and `rom_card` flash
partitions. The Mac II needs both its ROM and the card's.

Without flashing, the emulator also finds them in a `roms` folder on the SD
card, under any file name.
