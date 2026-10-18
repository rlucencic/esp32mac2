/*
 * The V8's Apple Sound Chip at $F14000: FIFO A only (1 KB of 8-bit
 * samples, offset binary), always in FIFO mode, played at 22.257 kHz (here
 * one sample per line, 22.255 kHz).
 *
 *   $000-$3FF  FIFO A (each write appends a sample)
 *   $800       version ($E8), $801-$803 read 1
 *   $804       FIFO status: bit 0 less than half full, bit 1 empty (or full)
 *   $803       bit 7 clears the FIFO
 *
 * The interrupt (pseudo-VIA bit 4) is a level: on while the FIFO is less
 * than half full. As MAME's asc_v8_device (R. Belmont, Doug Brown's
 * ASCTester results).
 *
 * The samples a frame plays are collected and go into a ring at the end of
 * the frame, which mac_audio_render() drains from the audio task.
 */
#include <stdlib.h>
#include <string.h>
#include "mac_internal.h"
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif

#define FIFO_SIZE   0x400
#define HALF        0x200

static struct {
    uint8_t fifo[FIFO_SIZE];
    uint16_t rd, wr, count;
    uint8_t regs[0x30];             /* $800-$82F */
    uint8_t status;
    int8_t last;                    /* the sample playing (held when empty) */
    uint32_t pos;                   /* samples of this frame played */
    int8_t frame[FRAME_LINES];
} asc;

static void update_status(void)
{
    if (asc.count < HALF) {
        asc.status |= 0x01;
    } else {
        asc.status &= ~0x01;
    }
    if (asc.count == 0 || asc.count >= FIFO_SIZE - 1) {
        asc.status |= 0x02;
    } else {
        asc.status &= ~0x02;
    }
    pvia_asc_irq(asc.count < HALF);
}

/* Play the samples due by now */
static void advance(void)
{
    uint64_t t = mac_now() - mac.frame_start;
    uint32_t upto = t / LINE_CYCLES;
    if (upto > FRAME_LINES) {
        upto = FRAME_LINES;
    }
    if (asc.pos >= upto) {
        return;
    }
    while (asc.pos < upto) {
        if (asc.count) {
            asc.last = (int8_t)(asc.fifo[asc.rd] ^ 0x80);
            asc.rd = (asc.rd + 1) & (FIFO_SIZE - 1);
            asc.count--;
        }
        asc.frame[asc.pos++] = asc.last;
    }
    update_status();
}

/* The next time the status changes on its own: the FIFO falls below half,
 * or runs empty
 */
static void schedule(void)
{
    if (asc.count == 0) {
        mac_schedule(EV_ASC, EV_NEVER);
        return;
    }
    uint32_t n = asc.count >= HALF ? asc.count - HALF + 1 : asc.count;
    mac_schedule(EV_ASC, mac.frame_start + (uint64_t)(asc.pos + n) * LINE_CYCLES);
}

void asc_event(void)
{
    advance();
    schedule();
}

void asc_reset(void)
{
    memset(&asc, 0, sizeof(asc));
    update_status();
    mac_schedule(EV_ASC, EV_NEVER);
}

uint8_t asc_read(uint32_t address)
{
    uint32_t offset = address & 0xFFF;
    if (offset < FIFO_SIZE) {
        return asc.fifo[offset];
    }
    if (offset < 0x800) {
        return 0;
    }
    uint32_t r = offset - 0x800;
    switch (r) {
    case 0x00:
        return 0xE8;
    case 0x01: case 0x02: case 0x03:
        return 1;
    case 0x04:
        advance();
        return asc.status;
    case 0x05: case 0x07:
        return 0;
    case 0x709: case 0x729:
        return 1;
    default:
        return r < sizeof(asc.regs) ? asc.regs[r] : 0;
    }
}

void asc_write(uint32_t address, uint8_t value)
{
    uint32_t offset = address & 0xFFF;
    if (offset < FIFO_SIZE) {
        advance();
        if (asc.count < FIFO_SIZE) {
            asc.fifo[asc.wr] = value;
            asc.wr = (asc.wr + 1) & (FIFO_SIZE - 1);
            asc.count++;
        }
        update_status();
        schedule();
        return;
    }
    if (offset < 0x800) {
        return;                     /* FIFO B: not on the V8 */
    }
    uint32_t r = offset - 0x800;
    switch (r) {
    case 0x01: case 0x02: case 0x05: case 0x07:
        return;
    case 0x03:
        if (value & 0x80) {
            advance();
            asc.rd = asc.wr = asc.count = 0;
            update_status();
            schedule();
        }
        break;
    }
    if (r < sizeof(asc.regs)) {
        asc.regs[r] = value;
    }
}

/* ---- Output ---- */

#define RING        4096            /* stereo frames, power of two */

static bool s_enabled;
static int16_t *s_ring;             /* RING stereo frames, made when sound is on */
static volatile uint32_t s_head, s_tail;

void mac_audio_enable(bool enable)
{
    if (enable && s_ring == NULL) {
        size_t bytes = RING * 2 * sizeof(int16_t);
#ifdef ESP_PLATFORM
        s_ring = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
#else
        s_ring = malloc(bytes);
#endif
    }
    s_enabled = enable && s_ring != NULL;
}

void asc_frame_end(void)
{
    advance();
    asc.pos = 0;
    if (!s_enabled) {
        return;
    }
    uint32_t tail = __atomic_load_n(&s_tail, __ATOMIC_ACQUIRE);
    if (s_head - tail > RING - FRAME_LINES) {
        return;                     /* nobody is listening: drop the frame */
    }
    for (int i = 0; i < FRAME_LINES; i++) {
        int16_t s = (int16_t)(asc.frame[i] * 160);
        uint32_t j = (s_head + i) % RING;
        s_ring[j * 2] = s_ring[j * 2 + 1] = s;
    }
    __atomic_store_n(&s_head, s_head + FRAME_LINES, __ATOMIC_RELEASE);
}

int mac_audio_render(int16_t *dst, int frames)
{
    if (s_ring == NULL) {
        return 0;
    }
    uint32_t head = __atomic_load_n(&s_head, __ATOMIC_ACQUIRE);
    uint32_t avail = head - s_tail;
    int n = frames < (int)avail ? frames : (int)avail;
    for (int i = 0; i < n; i++) {
        uint32_t j = (s_tail + i) % RING;
        dst[i * 2] = s_ring[j * 2];
        dst[i * 2 + 1] = s_ring[j * 2 + 1];
    }
    __atomic_store_n(&s_tail, s_tail + n, __ATOMIC_RELEASE);
    return n;
}

int mac_audio_pending(void)
{
    return (int)(__atomic_load_n(&s_head, __ATOMIC_ACQUIRE) - __atomic_load_n(&s_tail, __ATOMIC_ACQUIRE));
}
