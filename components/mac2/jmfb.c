/*
 * The Macintosh Display Card 8*24 in NuBus slot $9 (the Mac II): 1 MB of
 * video RAM, the JMFB framebuffer controller, a CRTC, a RAMDAC with the
 * colour table, a clock synthesiser, and the declaration ROM (341-0868)
 * whose driver programs them all. Its registers and behaviour as MAME's
 * nubus_48gc.cpp (R. Belmont, Vas Crabb); the monitor is the 13" Hi-Res
 * (640 x 480) or the 12" RGB (512 x 384).
 *
 * In the slot's space ($Fs000000 in 32-bit mode; the first MB at $s00000
 * in 24-bit mode):
 *   $000000-$1FFFFF  video RAM: plain, or (control bit 2) packed 24-bit
 *                    colour, a pixel per long word ($00RRGGBB) in 3 bytes
 *   $200000          control (bits 9-11 monitor sense, 6 transfer on, 5
 *                    convolution, 2 packed RGB), $200004 the RAMDAC load
 *                    clock divider, $200008 base (32-byte units; 64 for
 *                    24 bits), $20000C stride (4-byte units; 8)
 *   $200100-$2001FF  CRTC: timing (vertical in half lines), $1C0 beam
 *                    status, $13C bit 1 VBL interrupt off, $148 write:
 *                    clear the interrupt
 *   $200200-$20020F  RAMDAC: $200 colour address, $204 colour data (R, G,
 *                    B), $208 mode (bits 1-4: 0 1 bit, 4 2 bits, 8 4 bits,
 *                    $C 8 bits, $D 24 bits)
 *   $200300-$20033F  clock synthesiser (write only)
 *   $FE0000-$FFFFFF  declaration ROM, byte lane 3 (byte i at i * 4 + 3)
 *
 * Registers take their value from whatever byte lanes the CPU drives (the
 * firmware relies on that); reads give the 32-bit value's lanes.
 */
#include <string.h>
#include "mac_internal.h"

#define VRAM_SIZE       0x100000
#define ROM_WINDOW      (MAC_CARD_ROM_SIZE * 4)
#define ROM_START       (0x1000000 - ROM_WINDOW)

/* Sense lines of the two monitors (MAME's table: 6 Hi-Res, 2 RGB 12") */
static const uint8_t hires_sense[4] = { 6, 2, 4, 6 };
static const uint8_t rgb12_sense[4] = { 2, 2, 0, 2 };

static struct {
    uint16_t control, preload;
    uint32_t base, stride;
    uint8_t colors[3], clutcnt, clutoffs;
    uint8_t ramdac_mode, ramdac_conv;
    uint16_t hhalf, hactive, hbporch, hsync, hfporch;
    uint16_t vactive, vbporch, vsync, vfporch;
    bool vbl_disable;
    uint16_t multiplier, modulus;
    uint8_t pdiv;
    uint8_t clut[256][3];
    uint16_t pens[256];             /* RGB565 */
    bool changed;
} jm;

static bool ctrl_transfer(void) { return jm.control & 0x0040; }
static bool ctrl_convolution(void) { return jm.control & 0x0020; }
static bool ctrl_rgb(void) { return jm.control & 0x0004; }

static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
}

void jmfb_reset(void)
{
    memset(&jm, 0, sizeof(jm));
    jm.vbl_disable = true;
    jm.control = 0x0002;
    jm.preload = 256 - 8;
    jm.stride = 80 / 4;
    jm.hactive = 286;
    jm.hbporch = 22;
    jm.hsync = 30;
    jm.hfporch = 18;
    jm.vactive = 1740;
    jm.vbporch = 78;
    jm.vsync = 6;
    jm.vfporch = 6;
    jm.multiplier = 190;
    jm.modulus = 19;
    jm.pdiv = 1;
    jm.changed = true;
    ii_set_vram_window(true);
}

/* ---- Registers ---- */

static uint8_t beam_status(void)
{
    uint32_t vtotal = jm.vactive + jm.vbporch + jm.vsync + jm.vfporch;
    if (vtotal == 0 || jm.vactive == 0) {
        return 0x0E;
    }
    /* The frame starts with the active area; the CRTC's count with sync */
    uint64_t t = mac_now() - mac.frame_start;
    if (t >= FRAME_CYCLES) {
        t = FRAME_CYCLES - 1;
    }
    uint32_t halflines = (uint32_t)(t * vtotal / FRAME_CYCLES);
    uint32_t h = (jm.vsync + jm.vbporch + halflines) % vtotal;
    uint8_t result = 0x0F;
    if (h < jm.vsync) {
        result &= ~0x04;
    } else if (h < (uint32_t)jm.vsync + jm.vbporch) {
        result &= ~0x02;
    } else if (h < (uint32_t)jm.vsync + jm.vbporch + jm.vactive) {
        result &= ~0x01;
    } else {
        result &= ~0x08;
    }
    /* Horizontal blanking: the last fifth of each line */
    uint64_t line = FRAME_CYCLES * 2 / vtotal;
    if (line && t % line > line * 4 / 5) {
        result |= 0x20;
    }
    return result;
}

uint32_t jmfb_read(uint32_t offset)
{
    offset &= 0xFFFFFC;
    switch (offset) {
    case 0x200000: {
        const uint8_t *table = mac.monitor == MAC_MONITOR_12 ? rgb12_sense : hires_sense;
        uint16_t sense = table[0];
        if (jm.control & 0x0800) {
            sense &= table[1];
        }
        if (jm.control & 0x0400) {
            sense &= table[2];
        }
        if (jm.control & 0x0200) {
            sense &= table[3];
        }
        return (jm.control & 0xF1FF) | (sense << 9);
    }
    case 0x200004: return jm.preload;
    case 0x200008: return jm.base;
    case 0x20000C: return jm.stride;
    case 0x200108: return jm.hhalf;
    case 0x20010C: return jm.hactive;
    case 0x200110: return jm.hbporch;
    case 0x200114: return jm.hsync;
    case 0x200118: return jm.hfporch;
    case 0x200124: return jm.vactive;
    case 0x200128: return jm.vbporch;
    case 0x20012C: return jm.vsync;
    case 0x200130: return jm.vfporch;
    case 0x2001C0: return beam_status();
    case 0x200200: return 0;        /* the colour address doesn't read back */
    default:       return 0;
    }
}

void jmfb_write(uint32_t offset, uint32_t value)
{
    offset &= 0xFFFFFC;
    uint16_t v16 = value & 0xFFFF;
    switch (offset) {
    case 0x200000:
        jm.control = v16 & 0x7FFF;  /* the reset bit reads as 0 */
        ii_set_vram_window(!ctrl_rgb());
        jm.changed = true;
        break;
    case 0x200004: jm.preload = v16 & 0xFF; break;
    case 0x200008: jm.base = v16; jm.changed = true; break;
    case 0x20000C: jm.stride = v16; jm.changed = true; break;
    case 0x200108: jm.hhalf = v16 & 0x0FFF; break;
    case 0x20010C: jm.hactive = v16 & 0x0FFF; break;
    case 0x200110: jm.hbporch = v16 & 0x0FFF; break;
    case 0x200114: jm.hsync = v16 & 0x0FFF; break;
    case 0x200118: jm.hfporch = v16 & 0x0FFF; break;
    case 0x200124: jm.vactive = v16 & 0x0FFF; break;
    case 0x200128: jm.vbporch = v16 & 0x0FFF; break;
    case 0x20012C: jm.vsync = v16 & 0x0FFF; break;
    case 0x200130: jm.vfporch = v16 & 0x0FFF; break;
    case 0x20013C:
        jm.vbl_disable = (v16 & 2) != 0;
        if (jm.vbl_disable) {
            ii_slot_irq(9, false);
        }
        break;
    case 0x200148:
        ii_slot_irq(9, false);
        break;
    case 0x200200:
        jm.clutoffs = value & 0xFF;
        jm.clutcnt = 0;
        break;
    case 0x200204:
        jm.colors[jm.clutcnt++] = value & 0xFF;
        if (jm.clutcnt == 3) {
            uint8_t *c = jm.clut[jm.clutoffs];
            memcpy(c, jm.colors, 3);
            jm.pens[jm.clutoffs] = rgb565(c[0], c[1], c[2]);
            jm.clutoffs++;
            jm.clutcnt = 0;
            jm.changed = true;
        }
        break;
    case 0x200208:
        jm.ramdac_mode = (value >> 1) & 0xF;
        jm.ramdac_conv = value & 1;
        jm.changed = true;
        break;
    default:
        if (offset >= 0x200300 && offset < 0x200340) {
            uint32_t r = (offset - 0x200300) / 4;
            uint8_t d = value & 0x0F;
            if (r < 4) {
                jm.multiplier = (jm.multiplier & ~(0x0F << (r * 4))) | (d << (r * 4));
            } else if (r < 7) {
                jm.modulus = (jm.modulus & ~(0x0F << ((r & 3) * 4))) | (d << ((r & 3) * 4));
            } else if (r == 9) {
                jm.pdiv = d;
            }
        }
        break;
    }
}

/* ---- The slot's space, a byte at a time ---- */

static bool in_registers(uint32_t offset)
{
    return offset >= 0x200000 && offset < 0x200340;
}

uint8_t jmfb_read8(uint32_t offset)
{
    offset &= 0xFFFFFF;
    if (offset < 0x200000) {
        if (ctrl_rgb()) {
            uint32_t pixel = offset / 4, lane = offset & 3;
            uint32_t at = pixel * 3 + lane - 1;
            return lane == 0 || at >= VRAM_SIZE ? 0 : mac.vram[at ^ 1];
        }
        return offset < VRAM_SIZE ? mac.vram[offset ^ 1] : 0;
    }
    if (in_registers(offset)) {
        return (jmfb_read(offset) >> (8 * (3 - (offset & 3)))) & 0xFF;
    }
    if (offset >= ROM_START) {
        uint32_t o = offset - ROM_START;
        return (o & 3) == 3 ? mac.card_rom[o / 4] : 0xFF;
    }
    return 0;
}

void jmfb_write8(uint32_t offset, uint8_t value)
{
    offset &= 0xFFFFFF;
    if (offset < 0x200000) {
        if (ctrl_rgb()) {
            uint32_t pixel = offset / 4, lane = offset & 3;
            uint32_t at = pixel * 3 + lane - 1;
            if (lane != 0 && at < VRAM_SIZE) {
                mac.vram[at ^ 1] = value;
                mac.vram_written = true;
            }
        } else if (offset < VRAM_SIZE) {
            mac.vram[offset ^ 1] = value;
            mac.vram_written = true;
        }
        return;
    }
    if (in_registers(offset)) {
        jmfb_write(offset, value);
    }
}

void jmfb_vbl(void)
{
    if (!jm.vbl_disable) {
        ii_slot_irq(9, true);
    }
}

/* ---- The picture ---- */

void jmfb_screen_size(int *width, int *height)
{
    if (mac.monitor == MAC_MONITOR_12) {
        *width = 512;
        *height = 384;
    } else {
        *width = 640;
        *height = 480;
    }
}

int jmfb_screen_depth(void)
{
    switch (jm.ramdac_mode) {
    case 0x0: return 1;
    case 0x4: return 2;
    case 0x8: return 4;
    case 0xC: return 8;
    default:  return 24;
    }
}

void jmfb_render_line(int y, uint16_t *dst)
{
    int w, h;
    jmfb_screen_size(&w, &h);
    if (y < 0 || y >= h || !ctrl_transfer()) {
        memset(dst, 0, w * sizeof(uint16_t));
        return;
    }
    const uint8_t *vram = mac.vram;
    const uint16_t *pens = jm.pens;
    bool direct = jm.ramdac_mode == 0xD;
    uint32_t base = jm.base << ((direct ? 6 : 5) + (ctrl_convolution() ? 1 : 0));
    uint32_t stride = jm.stride << (direct ? 3 : 2);
    uint32_t line = base + (uint32_t)y * stride;
    for (int x = 0; x < w;) {
        uint32_t o;
        switch (jm.ramdac_mode) {
        case 0x0: {
            o = line + x / 8;
            uint8_t b = o < VRAM_SIZE ? vram[o ^ 1] : 0;
            for (int i = 7; i >= 0; i--) {
                dst[x++] = pens[(b >> i) & 1];
            }
            break;
        }
        case 0x4: {
            o = line + x / 4;
            uint8_t b = o < VRAM_SIZE ? vram[o ^ 1] : 0;
            for (int i = 6; i >= 0; i -= 2) {
                dst[x++] = pens[(b >> i) & 3];
            }
            break;
        }
        case 0x8: {
            o = line + x / 2;
            uint8_t b = o < VRAM_SIZE ? vram[o ^ 1] : 0;
            dst[x++] = pens[b >> 4];
            dst[x++] = pens[b & 0x0F];
            break;
        }
        case 0xC:
            o = line + x;
            dst[x++] = pens[o < VRAM_SIZE ? vram[o ^ 1] : 0];
            break;
        default:
            o = line + x * 3;
            if (o + 2 < VRAM_SIZE) {
                dst[x++] = rgb565(vram[o ^ 1], vram[(o + 1) ^ 1], vram[(o + 2) ^ 1]);
            } else {
                dst[x++] = 0;
            }
            break;
        }
    }
}

bool jmfb_screen_changed(void)
{
    if (mac.vram_written || jm.changed) {
        mac.vram_written = false;
        jm.changed = false;
        return true;
    }
    return false;
}
