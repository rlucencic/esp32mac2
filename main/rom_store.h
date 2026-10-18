#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "mac.h"

#define ROM_MAX_SIZE    (512 * 1024)

/* Load the ROM for `model` into buf (ROM_MAX_SIZE bytes): from its flash
 * partition (rom_lc, rom_ii: roms/lc.rom, roms/ii.rom, written by idf.py
 * flash), else from a file in the SD card's "roms" folder whose checksum
 * is a ROM of that model. Its size in *size, its name in *name.
 */
bool rom_store_load(mac_model_t model, uint8_t *buf, uint32_t *size, const char **name);

/* The Mac II's video card ROM, the Display Card 8*24's (MAC_CARD_ROM_SIZE
 * bytes): partition rom_card (roms/mdc824.rom), else the "roms" folder
 */
bool rom_store_load_card(uint8_t *buf);
