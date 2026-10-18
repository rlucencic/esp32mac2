#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "mac.h"

/* Machine settings kept in NVS (namespace "mac2") */
typedef struct {
    uint16_t ram_mb;        /* 2, 4, 6, 10 */
    uint8_t monitor;        /* mac_monitor_t */
} settings_t;

extern settings_t settings;

void settings_load(void);
void settings_save(void);

/* Parameter RAM (MAC_PRAM_SIZE bytes): false if none was saved yet */
bool settings_load_pram(uint8_t *pram);
void settings_save_pram(const uint8_t *pram);
