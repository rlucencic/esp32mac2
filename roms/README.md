# Macintosh ROM images

Put your own ROM dump here (it is copyrighted and not part of this
repository). The emulator recognises it by its checksum (the first four
bytes), so only this file works:

| File | Machine | Size | Checksum |
|---|---|---|---|
| `lc.rom` | Macintosh LC | 512 KB | 350EACF0 |

`idf.py flash` writes it to the `rom_lc` flash partition.

Without flashing, the emulator also finds it in a `roms` folder on the SD
card, under any file name.
