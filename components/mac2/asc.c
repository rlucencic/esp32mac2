/*
 * The Apple Sound Chip: 2 KB of sample memory and registers at $800,
 * played at 22.257 kHz (here one sample per line, 22.255 kHz). Two of them,
 * as MAME's asc.cpp has them (R. Belmont; Doug Brown's ASCTester results):
 *
 *   V8 (LC)        FIFO A only (1 KB, 8-bit offset binary), always in FIFO
 *                  mode, version $E8; mode/control/FIFO mode read 1. The
 *                  interrupt is a level: on while FIFO A is less than half
 *                  full.
 *   original (II)  FIFO A (left) and B (right), mono or stereo (control
 *                  bit 1), or four wavetable voices ($810-$82F: 9.15
 *                  phase and increment, 24 bits each, over 512-byte tables
 *                  in the sample memory); version $00. The interrupt comes
 *                  when a FIFO falls to half full; reading the status
 *                  clears the flags and the interrupt.
 *
 *   $000-$3FF  FIFO A (each write appends a sample; in wavetable mode
 *              the memory itself), $400-$7FF FIFO B
 *   $800 version, $801 mode (0 off, 1 FIFO, 2 wavetable), $802 control,
 *   $803 FIFO mode (bit 7 clears the FIFOs), $804 FIFO status (bit 0 A
 *   half empty, 1 A empty or full, 2 and 3 B the same)
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

enum { R_VERSION, R_MODE, R_CONTROL, R_FIFOMODE, R_FIFOSTAT, R_WTCONTROL, R_VOLUME, R_CLOCK };

#define STAT_HALF_A     0x01
#define STAT_EDGE_A     0x02        /* empty or full */
#define STAT_HALF_B     0x04
#define STAT_EDGE_B     0x08
#define CONTROL_STEREO  0x02

static struct {
    bool v8;
    uint8_t fifo[2][FIFO_SIZE];
    uint16_t rd[2], wr[2], count[2];
    uint8_t regs[0x30];             /* $800-$82F */
    uint32_t phase[4], incr[4];     /* wavetable voices */
    bool irq;
    int16_t last[2];                /* the sample playing (held when empty) */
    int32_t dc_in[2], dc_out[2];    /* the output's DC blocker */
    uint32_t pos;                   /* samples of this frame played */
    int16_t frame[FRAME_LINES][2];
} asc;

static void set_irq(bool active)
{
    if (active != asc.irq) {
        asc.irq = active;
        asc_irq_changed(active);
    }
}

/* The V8's status follows the FIFO's level */
static void v8_status(void)
{
    uint8_t s = 0;
    if (asc.count[0] < HALF) {
        s |= STAT_HALF_A;
    }
    if (asc.count[0] == 0 || asc.count[0] >= FIFO_SIZE - 1) {
        s |= STAT_EDGE_A;
    }
    asc.regs[R_FIFOSTAT] = s;
    set_irq(asc.count[0] < HALF);
}

static bool stereo(void)
{
    return !asc.v8 && (asc.regs[R_CONTROL] & CONTROL_STEREO);
}

static int8_t fifo_sample(int ch)
{
    if (asc.count[ch]) {
        uint16_t before = asc.count[ch];
        int8_t s = (int8_t)(asc.fifo[ch][asc.rd[ch]] ^ 0x80);
        asc.rd[ch] = (asc.rd[ch] + 1) & (FIFO_SIZE - 1);
        asc.count[ch]--;
        if (!asc.v8 && before == HALF) {
            /* The original ASC: the flag and the interrupt as it falls to half */
            asc.regs[R_FIFOSTAT] |= ch ? STAT_HALF_B : STAT_HALF_A;
            asc.regs[R_FIFOSTAT] &= ~(ch ? STAT_EDGE_B : STAT_EDGE_A);
            set_irq(true);
        }
        return s;
    }
    return (int8_t)(asc.last[ch] / 256);
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
    int mode = asc.v8 ? 1 : asc.regs[R_MODE] & 3;
    while (asc.pos < upto) {
        if (mode == 1) {
            int8_t l = fifo_sample(0);
            int8_t r = stereo() ? fifo_sample(1) : l;
            asc.last[0] = l * 256;
            asc.last[1] = r * 256;
        } else if (mode == 2) {
            int mix = 0;
            for (int v = 0; v < 4; v++) {
                asc.phase[v] = (asc.phase[v] + asc.incr[v]) & 0xFFFFFF;
                const uint8_t *table = asc.fifo[v >> 1] + (v & 1) * 0x200;
                mix += (int8_t)(table[(asc.phase[v] >> 15) & 0x1FF] ^ 0x80);
            }
            asc.last[0] = asc.last[1] = (int16_t)(mix * 64);
        }   /* off: the last level stays (the DC blocker fades it out) */
        asc.frame[asc.pos][0] = asc.last[0];
        asc.frame[asc.pos][1] = asc.last[1];
        asc.pos++;
    }
    if (asc.v8) {
        v8_status();
    }
}

/* The next time the status changes on its own: a FIFO falls to half, or
 * runs empty
 */
static void schedule(void)
{
    uint32_t n = 0;
    for (int ch = 0; ch < (stereo() ? 2 : 1); ch++) {
        uint32_t c = asc.count[ch];
        uint32_t m = c > HALF ? c - HALF : c;
        if (c && (n == 0 || m < n)) {
            n = m;
        }
    }
    mac_schedule(EV_ASC, n ? mac.frame_start + (uint64_t)(asc.pos + n) * LINE_CYCLES : EV_NEVER);
}

void asc_event(void)
{
    advance();
    schedule();
}

static void clear_fifos(void)
{
    memset(asc.rd, 0, sizeof(asc.rd));
    memset(asc.wr, 0, sizeof(asc.wr));
    memset(asc.count, 0, sizeof(asc.count));
}

void asc_reset(bool v8)
{
    memset(&asc, 0, sizeof(asc));
    asc.v8 = v8;
    if (v8) {
        asc.regs[R_MODE] = 1;
        v8_status();
    } else {
        asc_irq_changed(false);
    }
    mac_schedule(EV_ASC, EV_NEVER);
}

uint8_t asc_read(uint32_t address)
{
    uint32_t offset = address & 0xFFF;
    if (offset < 0x800) {
        return asc.fifo[offset >> 10][offset & (FIFO_SIZE - 1)];
    }
    uint32_t r = offset - 0x800;
    if (asc.v8) {
        switch (r) {
        case R_VERSION: return 0xE8;
        case R_MODE: case R_CONTROL: case R_FIFOMODE: return 1;
        case R_FIFOSTAT: advance(); return asc.regs[R_FIFOSTAT];
        case R_WTCONTROL: case R_CLOCK: return 0;
        case 0x709: case 0x729: return 1;
        default: return r < sizeof(asc.regs) ? asc.regs[r] : 0;
        }
    }
    switch (r) {
    case R_VERSION:
        return 0x00;
    case R_FIFOSTAT: {
        advance();
        uint8_t s = asc.regs[R_FIFOSTAT];
        asc.regs[R_FIFOSTAT] = 0;   /* reading clears it, and the interrupt */
        set_irq(false);
        return s;
    }
    case 0x709: case 0x729:
        return 1;
    default:
        break;
    }
    if (r >= 0x10 && r < 0x30) {
        /* The voices' phase and increment, 24 bits big-endian after a pad byte */
        int v = (r - 0x10) >> 3;
        uint32_t value = (r & 4) ? asc.incr[v] : asc.phase[v];
        int byte = r & 3;
        return byte == 0 ? 0 : (value >> (8 * (3 - byte))) & 0xFF;
    }
    return r < sizeof(asc.regs) ? asc.regs[r] : (offset >= 0x1000 ? 0xFF : 0);
}

static void fifo_write(int ch, uint8_t value)
{
    if (asc.count[ch] < FIFO_SIZE) {
        asc.fifo[ch][asc.wr[ch]] = value;
        asc.wr[ch] = (asc.wr[ch] + 1) & (FIFO_SIZE - 1);
        asc.count[ch]++;
    }
    if (!asc.v8) {
        uint8_t half = ch ? STAT_HALF_B : STAT_HALF_A, edge = ch ? STAT_EDGE_B : STAT_EDGE_A;
        if (asc.count[ch] >= HALF) {
            asc.regs[R_FIFOSTAT] &= ~half;
            if (asc.count[ch] >= FIFO_SIZE - 1) {
                asc.regs[R_FIFOSTAT] |= edge;
            }
        } else {
            asc.regs[R_FIFOSTAT] &= ~edge;
        }
    }
}

void asc_write(uint32_t address, uint8_t value)
{
    uint32_t offset = address & 0xFFF;
    if (offset < 0x800) {
        int ch = offset >> 10;
        advance();
        if (asc.v8) {
            if (ch == 0) {          /* FIFO B: not on the V8 */
                fifo_write(0, value);
                v8_status();
            }
        } else if ((asc.regs[R_MODE] & 3) == 2) {
            asc.fifo[ch][offset & (FIFO_SIZE - 1)] = value;
        } else {
            fifo_write(ch, value);
        }
        schedule();
        return;
    }
    uint32_t r = offset - 0x800;
    if (asc.v8 && (r == R_MODE || r == R_CONTROL || r == R_WTCONTROL || r == R_CLOCK)) {
        return;
    }
    advance();
    switch (r) {
    case R_MODE:
        value &= 3;
        if (value != asc.regs[R_MODE]) {
            clear_fifos();
        }
        break;
    case R_FIFOMODE:
        if (value & 0x80) {
            clear_fifos();
            if (asc.v8) {
                v8_status();
            } else {
                asc.regs[R_FIFOSTAT] |= STAT_EDGE_A | STAT_EDGE_B;
            }
        }
        break;
    default:
        if (r >= 0x10 && r < 0x30 && (r & 3)) {
            int v = (r - 0x10) >> 3;
            uint32_t *target = (r & 4) ? &asc.incr[v] : &asc.phase[v];
            int shift = 8 * (3 - (r & 3));
            *target = (*target & ~(0xFFu << shift)) | ((uint32_t)value << shift);
        }
        break;
    }
    if (r < sizeof(asc.regs)) {
        asc.regs[r] = value;
    }
    schedule();
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
    /* The Macs' audio output is AC coupled: a DC blocker (a high pass at
     * ~20 Hz) takes out the offset the idle wavetable voices add
     */
    for (int i = 0; i < FRAME_LINES; i++) {
        uint32_t j = (s_head + i) % RING;
        for (int ch = 0; ch < 2; ch++) {
            int32_t x = asc.frame[i][ch] * 5 / 8;
            int32_t y = x - asc.dc_in[ch] + (asc.dc_out[ch] * 4037) / 4096;
            asc.dc_in[ch] = x;
            asc.dc_out[ch] = y;
            s_ring[j * 2 + ch] = (int16_t)(y > 32767 ? 32767 : y < -32768 ? -32768 : y);
        }
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
