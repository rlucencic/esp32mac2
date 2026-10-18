/*
 * Memory map and the Musashi bus callbacks. The V8 decodes A23-A0 (and A31:
 * in 32-bit mode an address with A31 set reaches nothing; in 24-bit mode
 * the HMMU drops it):
 *
 *   $000000-$9FFFFF  RAM, as the RAM configuration places it (the ROM while
 *                    the overlay is on); the board's 2 MB is always also
 *                    at $800000
 *   $A00000-$AFFFFF  ROM, repeated (the first read here ends the overlay)
 *   $F00000-$F01FFF  VIA1 (register n at + n * $200)
 *   $F04000-$F05FFF  SCC
 *   $F10000-$F11FFF  SCSI 5380 (register n at + n * $10)
 *   $F14000-$F15FFF  ASC
 *   $F16000-$F17FFF  SWIM (register n at + n * $200)
 *   $F24000-$F25FFF  Ariel colour table
 *   $F26000-$F27FFF  pseudo-VIA
 *   $F40000-$FBFFFF  video RAM
 *
 * Unmapped addresses read 0 (no bus errors), as in MAME, whose V8 the ROM
 * is happy with. The 68020 also reads and writes words and longs at odd
 * addresses: those take the byte-wise path.
 */
#include <string.h>
#include "m68k.h"
#include "mac_internal.h"

#define MB      0x100000u

static inline uint16_t rd16(const uint8_t *mem, uint32_t offset)
{
    return *(const uint16_t *)(mem + offset);
}

static inline void wr16(uint8_t *mem, uint32_t offset, uint16_t value)
{
    *(uint16_t *)(mem + offset) = value;
}

/* ---- RAM configuration (pseudo-VIA register 1) ---- */

static void update_window(void)
{
    uint32_t pages = 0;
    if (!mac.overlay) {
        while (pages < RAM_PAGES && (pages + 1) * MB <= mac.ram_size &&
               mac.ram_map[pages] == mac.ram + pages * MB) {
            pages++;
        }
    }
    mac.ram_window = pages * MB;
}

static void map_ram(uint32_t address, uint8_t *host, uint32_t size)
{
    for (uint32_t a = address; a < address + size && a < RAM_PAGES * MB; a += MB) {
        mac.ram_map[a / MB] = host + (a - address);
    }
}

/* Bits 6-7: SIMM size (none, 2, 4, 8 MB), bit 5: only 2 MB on the board.
 * The SIMMs go at 0, the board's RAM after the size the bits say (as in
 * MAME's V8, which the ROM's RAM sizing works with).
 */
void mem_set_ram_config(uint8_t config)
{
    if (mac.overlay) {
        return;
    }
    static const uint32_t simm_sizes[4] = { 0, 2 * MB, 4 * MB, 8 * MB };
    uint8_t *board = mac.ram + mac.simm_size;
    memset(mac.ram_map, 0, sizeof(mac.ram_map));
    map_ram(0x800000, board, BOARD_RAM);
    uint32_t board_at = 0;
    if (mac.simm_size > 0 && (config & 0xC0)) {
        map_ram(0, mac.ram, mac.simm_size);
        board_at = simm_sizes[config >> 6];
    }
    if ((config & 0xC0) != 0xC0) {
        map_ram(board_at, board, BOARD_RAM);
    }
    update_window();
}

void mem_set_overlay(bool on)
{
    if (on == mac.overlay) {
        return;
    }
    mac.overlay = on;
    if (on) {
        memset(mac.ram_map, 0, sizeof(mac.ram_map));
        update_window();
    } else {
        mem_set_ram_config(0xC0);
    }
}

void mem_set_24bit(bool on)
{
    mac.addr_mask = on ? 0x00FFFFFF : 0x80FFFFFF;
}

void mem_reset(void)
{
    mac.overlay = false;
    mem_set_overlay(true);
    mem_set_24bit(false);
}

/* ---- I/O ---- */

#ifdef ESP_PLATFORM
#define IO_LOG(...)
#else
#include <stdlib.h>
/* Host build: MAC_IO=1 logs every I/O access (MAC_IO=<hex mask> of the
 * 8 KB blocks: bit n = $F00000 + n * $2000)
 */
static int64_t s_io_log = -1;
#define IO_LOG(a, w, v) do { \
        if (s_io_log < 0) s_io_log = getenv("MAC_IO") ? strtoll(getenv("MAC_IO"), NULL, 16) : 0; \
        if (s_io_log && ((s_io_log == 1) || (s_io_log >> (((a) >> 13) & 0x3F) & 1))) \
            printf("io f%u pc %08x %s %06x %02x\n", (unsigned)mac.frames, m68k_get_reg(NULL, M68K_REG_PPC), \
                   (w) ? "W" : "R", (unsigned)(a), (unsigned)(v)); \
    } while (0)
#endif

static uint8_t io_read8_(uint32_t a)
{
    switch ((a >> 13) & 0x7F) {
    case 0x00: return via_read(a);
    case 0x02: return scc_read(a);
    case 0x08: return scsi_read(a);
    case 0x0A: return asc_read(a);
    case 0x0B: return (a & 1) ? 0 : swim_access(a, false, 0);
    case 0x12: return ariel_read(a);
    case 0x13: return pvia_read(a);
    default:   return 0;
    }
}

static uint8_t io_read8(uint32_t a)
{
    uint8_t v = io_read8_(a);
    IO_LOG(a, 0, v);
    return v;
}

static void io_write8(uint32_t a, uint8_t v)
{
    IO_LOG(a, 1, v);
    switch ((a >> 13) & 0x7F) {
    case 0x00: via_write(a, v); break;
    case 0x02: scc_write(a, v); break;
    case 0x08: scsi_write(a, v); break;
    case 0x0A: asc_write(a, v); break;
    case 0x0B: swim_access(a, true, v); break;
    case 0x12: ariel_write(a, v); break;
    case 0x13: pvia_write(a, v); break;
    default:   break;
    }
}

/* ---- Byte-wise path for everything but the fast cases ---- */

static uint8_t slow_read8(uint32_t a)
{
    if (a >= 0x80000000) {
        return 0;
    }
    if (a < ROM_BASE) {
        if (mac.overlay) {
            return mac.rom[(a & mac.rom_mask) ^ 1];
        }
        const uint8_t *p = mac.ram_map[a >> 20];
        return p ? p[(a & (MB - 1)) ^ 1] : 0;
    }
    if (a < ROM_BASE + MB) {
        if (mac.overlay) {
            mem_set_overlay(false);
        }
        return mac.rom[(a & mac.rom_mask) ^ 1];
    }
    if (a < 0xF00000) {
        return 0;
    }
    uint32_t v = a - VRAM_BASE;
    if (v < MAC_VRAM_SIZE) {
        return mac.vram[v ^ 1];
    }
    return io_read8(a);
}

static void slow_write8(uint32_t a, uint8_t value)
{
    if (a >= 0x80000000 || (a >= ROM_BASE && a < 0xF00000)) {
        return;
    }
    if (a < ROM_BASE) {
        uint8_t *p = mac.overlay ? NULL : mac.ram_map[a >> 20];
        if (p) {
            p[(a & (MB - 1)) ^ 1] = value;
        }
        return;
    }
    uint32_t v = a - VRAM_BASE;
    if (v < MAC_VRAM_SIZE) {
        mac.vram[v ^ 1] = value;
        mac.vram_written = true;
        return;
    }
    io_write8(a, value);
}

static uint16_t slow_read16(uint32_t a)
{
    uint16_t hi = slow_read8(a);
    return (hi << 8) | slow_read8((a + 1) & mac.addr_mask);
}

static void slow_write16(uint32_t a, uint16_t v)
{
    slow_write8(a, v >> 8);
    slow_write8((a + 1) & mac.addr_mask, v & 0xFF);
}

/* ---- Musashi callbacks: fast paths for RAM, ROM and video RAM ---- */

unsigned int MAC_HOT m68k_read_memory_8(unsigned int address)
{
    uint32_t a = address & mac.addr_mask;
    if (a < mac.ram_window) {
        return mac.ram[a ^ 1];
    }
    return slow_read8(a);
}

unsigned int MAC_HOT m68k_read_memory_16(unsigned int address)
{
    uint32_t a = address & mac.addr_mask;
    if (!(a & 1)) {
        if (a < mac.ram_window) {
            return rd16(mac.ram, a);
        }
        if ((a & 0x80F00000) == ROM_BASE && !mac.overlay) {
            return rd16(mac.rom, a & mac.rom_mask);
        }
        uint32_t v = a - VRAM_BASE;
        if (v < MAC_VRAM_SIZE) {
            return rd16(mac.vram, v);
        }
    }
    return slow_read16(a);
}

unsigned int MAC_HOT m68k_read_memory_32(unsigned int address)
{
    uint32_t a = address & mac.addr_mask;
    if (!(a & 1)) {
        if (a + 2 < mac.ram_window) {
            return ((uint32_t)rd16(mac.ram, a) << 16) | rd16(mac.ram, a + 2);
        }
        if ((a & 0x80F00000) == ROM_BASE && !mac.overlay) {
            uint32_t o = a & mac.rom_mask;
            if (o <= mac.rom_mask - 3) {
                return ((uint32_t)rd16(mac.rom, o) << 16) | rd16(mac.rom, o + 2);
            }
        }
        uint32_t v = a - VRAM_BASE;
        if (v < MAC_VRAM_SIZE - 2) {
            return ((uint32_t)rd16(mac.vram, v) << 16) | rd16(mac.vram, v + 2);
        }
    }
    uint32_t hi = slow_read16(a);
    return (hi << 16) | slow_read16((a + 2) & mac.addr_mask);
}

void MAC_HOT m68k_write_memory_8(unsigned int address, unsigned int value)
{
    uint32_t a = address & mac.addr_mask;
    if (a < mac.ram_window) {
        mac.ram[a ^ 1] = value;
        return;
    }
    slow_write8(a, value);
}

void MAC_HOT m68k_write_memory_16(unsigned int address, unsigned int value)
{
    uint32_t a = address & mac.addr_mask;
    if (!(a & 1)) {
        if (a < mac.ram_window) {
            wr16(mac.ram, a, value);
            return;
        }
        uint32_t v = a - VRAM_BASE;
        if (v < MAC_VRAM_SIZE) {
            wr16(mac.vram, v, value);
            mac.vram_written = true;
            return;
        }
    }
    slow_write16(a, value);
}

void MAC_HOT m68k_write_memory_32(unsigned int address, unsigned int value)
{
    uint32_t a = address & mac.addr_mask;
    if (!(a & 1)) {
        if (a + 2 < mac.ram_window) {
            wr16(mac.ram, a, value >> 16);
            wr16(mac.ram, a + 2, value & 0xFFFF);
            return;
        }
        uint32_t v = a - VRAM_BASE;
        if (v < MAC_VRAM_SIZE - 2) {
            wr16(mac.vram, v, value >> 16);
            wr16(mac.vram, v + 2, value & 0xFFFF);
            mac.vram_written = true;
            return;
        }
    }
    slow_write16(a, value >> 16);
    slow_write16((a + 2) & mac.addr_mask, value & 0xFFFF);
}

/* ---- Side-effect-free access for the emulator (24-bit addresses) ---- */

uint16_t mac_peek16(uint32_t a)
{
    a &= 0xFFFFFE;
    if (a < ROM_BASE) {
        if (mac.overlay) {
            return rd16(mac.rom, a & mac.rom_mask);
        }
        const uint8_t *p = mac.ram_map[a >> 20];
        return p ? rd16(p, a & (MB - 1)) : 0;
    }
    if (a < ROM_BASE + MB) {
        return rd16(mac.rom, a & mac.rom_mask);
    }
    uint32_t v = a - VRAM_BASE;
    if (v < MAC_VRAM_SIZE) {
        return rd16(mac.vram, v);
    }
    return 0;
}

uint8_t mac_peek8(uint32_t a)
{
    uint16_t w = mac_peek16(a & ~1u);
    return (a & 1) ? (w & 0xFF) : (w >> 8);
}

uint32_t mac_peek32(uint32_t a)
{
    return ((uint32_t)mac_peek16(a) << 16) | mac_peek16(a + 2);
}

/* Writes go to RAM only */
void mac_poke8(uint32_t a, uint8_t v)
{
    a &= 0xFFFFFF;
    uint8_t *p = a < ROM_BASE && !mac.overlay ? mac.ram_map[a >> 20] : NULL;
    if (p) {
        p[(a & (MB - 1)) ^ 1] = v;
    }
}

void mac_poke16(uint32_t a, uint16_t v)
{
    mac_poke8(a, v >> 8);
    mac_poke8(a + 1, v & 0xFF);
}

void mac_poke32(uint32_t a, uint32_t v)
{
    mac_poke16(a, v >> 16);
    mac_poke16(a + 2, v & 0xFFFF);
}

uint32_t mac_ram_top(void)
{
    return mac.ram_size;
}

/* Disk transfers: within the RAM that maps straight (all of it once the
 * ROM has configured it)
 */
bool mac_ram_range(uint32_t a, uint32_t len)
{
    return a < mac.ram_window && len <= mac.ram_window - a;
}

void mac_ram_write(uint32_t a, const uint8_t *src, uint32_t len)
{
    if ((a & 1) == 0) {
        /* Whole words: swap pairs */
        uint32_t i = 0;
        for (; i + 1 < len; i += 2) {
            mac.ram[a + i] = src[i + 1];
            mac.ram[a + i + 1] = src[i];
        }
        if (i < len) {
            mac.ram[(a + i) ^ 1] = src[i];
        }
        return;
    }
    for (uint32_t i = 0; i < len; i++) {
        mac.ram[(a + i) ^ 1] = src[i];
    }
}

void mac_ram_read(uint32_t a, uint8_t *dst, uint32_t len)
{
    if ((a & 1) == 0) {
        uint32_t i = 0;
        for (; i + 1 < len; i += 2) {
            dst[i] = mac.ram[a + i + 1];
            dst[i + 1] = mac.ram[a + i];
        }
        if (i < len) {
            dst[i] = mac.ram[(a + i) ^ 1];
        }
        return;
    }
    for (uint32_t i = 0; i < len; i++) {
        dst[i] = mac.ram[(a + i) ^ 1];
    }
}

unsigned int m68k_read_disassembler_16(unsigned int a)
{
    return mac_peek16(a);
}

unsigned int m68k_read_disassembler_32(unsigned int a)
{
    return mac_peek32(a);
}

/* ---- Free memory, from the Memory Manager's zone headers ---- */

#define LM_BUFPTR       0x010C
#define LM_HEAPEND      0x0114
#define LM_APPLLIMIT    0x0130
#define LM_SYSZONE      0x02A6
#define LM_APPLZONE     0x02AA
#define ZONE_BKLIM      0       /* end of the zone */
#define ZONE_ZCBFREE    12      /* free bytes in it */
#define ZONE_HEADER     52

static uint32_t ptr24(uint32_t a)
{
    return mac_peek32(a) & 0xFFFFFF;
}

/* A zone header at z ending at or below `top`: its free bytes, or -1 */
static int64_t zone_free(uint32_t z, uint32_t top)
{
    if (z == 0 || (z & 1) || z + ZONE_HEADER >= mac.ram_size) {
        return -1;
    }
    uint32_t lim = ptr24(z + ZONE_BKLIM);
    uint32_t free_bytes = mac_peek32(z + ZONE_ZCBFREE);
    if (lim <= z + ZONE_HEADER || lim > top || free_bytes >= lim - z) {
        return -1;
    }
    return free_bytes;
}

bool mac_ram_free(uint32_t *free_bytes, uint32_t *system_free)
{
    *free_bytes = *system_free = 0;
    if (mac.ram == NULL || mac.overlay || mac_peek32(LM_TICKS) == 0) {
        return false;
    }
    uint32_t top = ptr24(LM_BUFPTR);
    uint32_t sys = ptr24(LM_SYSZONE);
    if (top == 0 || top > mac.ram_size) {
        return false;
    }
    int64_t sys_free = zone_free(sys, top);
    if (sys_free < 0) {
        return false;
    }
    *system_free = sys_free;

    /* System 7: the Process Manager's heap follows the system heap (after
     * its 12-byte end block) and reaches up to just below BufPtr
     */
    uint32_t sys_end = ptr24(sys + ZONE_BKLIM);
    for (uint32_t z = sys_end; z <= sys_end + 64; z += 2) {
        int64_t f = zone_free(z, top);
        if (f >= 0 && ptr24(z + ZONE_BKLIM) + 0x10000 >= top) {
            *free_bytes = f;
            return true;
        }
    }

    /* Otherwise: the application heap, and the room it can still grow into */
    uint32_t app = ptr24(LM_APPLZONE);
    int64_t app_free = zone_free(app, top);
    if (app_free < 0) {
        return false;
    }
    uint32_t heap_end = ptr24(LM_HEAPEND), limit = ptr24(LM_APPLLIMIT);
    *free_bytes = app_free + (limit > heap_end && limit <= top ? limit - heap_end : 0);
    return true;
}
