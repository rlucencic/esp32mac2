/*
 * The built-in video: the V8 reads the picture from its own 512 KB of video
 * RAM (1024 bytes a line up to 8 bits per pixel, 2 bytes a pixel at 16)
 * and the Ariel RAMDAC's colour table turns pixel values into colours.
 *
 * Ariel at $F24000: register 0 the table address (writing it, or reading
 * it, starts a red-green-blue triple), 1 the data (red, green, blue, then
 * the next entry), 2 control, 3 key colour. With fewer bits per pixel the
 * V8 fills the low bits with 1s: 1 bit uses entries $7F and $FF, 2 bits
 * $3F/$7F/$BF/$FF and so on (as MAME's V8).
 */
#include <string.h>
#include "mac_internal.h"

static struct {
    uint8_t address, component, control;
    uint8_t clut[256][3];
    uint16_t pens[256];             /* RGB565 */
    bool changed;
} ar;

static uint32_t s_signature = 1;

static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
}

void video_reset(void)
{
    memset(&ar, 0, sizeof(ar));
    /* Until the ROM sets it: a grey ramp */
    for (int i = 0; i < 256; i++) {
        ar.clut[i][0] = ar.clut[i][1] = ar.clut[i][2] = 255 - i;
        ar.pens[i] = rgb565(255 - i, 255 - i, 255 - i);
    }
    ar.changed = true;
}

void video_mode_changed(void)
{
    ar.changed = true;
}

static void next_component(void)
{
    if (++ar.component == 3) {
        ar.component = 0;
        ar.address++;
    }
}

uint8_t ariel_read(uint32_t address)
{
    switch (address & 0x1FFF) {
    case 0:
        ar.component = 0;
        return ar.address;
    case 1: {
        uint8_t v = ar.clut[ar.address][ar.component];
        next_component();
        return v;
    }
    case 2:
    case 3:
        return ar.control;
    default:
        return 0;
    }
}

void ariel_write(uint32_t address, uint8_t value)
{
    switch (address & 0x1FFF) {
    case 0:
        ar.address = value;
        ar.component = 0;
        break;
    case 1: {
        uint8_t *c = ar.clut[ar.address];
        c[ar.component] = value;
        if (ar.component == 2) {
            ar.pens[ar.address] = rgb565(c[0], c[1], c[2]);
            ar.changed = true;
        }
        next_component();
        break;
    }
    case 2:
    case 3:
        ar.control = value;
        break;
    }
}

void mac_screen_size(int *width, int *height)
{
    if (mac.monitor == MAC_MONITOR_12) {
        *width = 512;
        *height = 384;
    } else {
        *width = 640;
        *height = 480;
    }
}

int mac_screen_depth(void)
{
    int mode = pvia_video_config() & 7;
    return mode <= 4 ? 1 << mode : 1;
}

void mac_render_line_rgb565(int y, uint16_t *dst)
{
    int w, h;
    mac_screen_size(&w, &h);
    if (y < 0 || y >= h || mac.vram == NULL) {
        memset(dst, 0, w * sizeof(uint16_t));
        return;
    }
    const uint8_t *vram = mac.vram;
    const uint16_t *pens = ar.pens;
    int mode = pvia_video_config() & 7;
    if (mode == 4) {
        /* 16 bits: x RRRRR GGGGG BBBBB, not through the colour table */
        uint32_t line = (uint32_t)y * w * 2;
        for (int x = 0; x < w; x++) {
            uint32_t o = line + x * 2;
            uint16_t p = o < MAC_VRAM_SIZE ? *(const uint16_t *)(vram + o) : 0;
            uint16_t g = (p >> 5) & 0x1F;
            *dst++ = ((p >> 10) & 0x1F) << 11 | g << 6 | (g >> 4) << 5 | (p & 0x1F);
        }
        return;
    }
    uint32_t line = (uint32_t)y * 1024;
    switch (mode) {
    case 0:
        for (int x = 0; x < w / 8; x++) {
            uint8_t b = vram[(line + x) ^ 1];
            for (int i = 7; i >= 0; i--) {
                *dst++ = pens[0x7F | (((b >> i) & 1) << 7)];
            }
        }
        break;
    case 1:
        for (int x = 0; x < w / 4; x++) {
            uint8_t b = vram[(line + x) ^ 1];
            for (int i = 6; i >= 0; i -= 2) {
                *dst++ = pens[0x3F | (((b >> i) & 3) << 6)];
            }
        }
        break;
    case 2:
        for (int x = 0; x < w / 2; x++) {
            uint8_t b = vram[(line + x) ^ 1];
            *dst++ = pens[0x0F | (b & 0xF0)];
            *dst++ = pens[0x0F | ((b << 4) & 0xF0)];
        }
        break;
    default:
        for (int x = 0; x < w; x++) {
            *dst++ = pens[vram[(line + x) ^ 1]];
        }
        break;
    }
}

uint32_t mac_screen_signature(void)
{
    if (mac.vram_written || ar.changed) {
        mac.vram_written = false;
        ar.changed = false;
        s_signature++;
    }
    return s_signature;
}
