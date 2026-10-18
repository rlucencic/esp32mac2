/*
 * The Macintosh II: its memory map, the RAM banks, the 24-bit address
 * translation, the two VIAs' wiring, NuBus with the 8*24 card in slot $9.
 * As MAME's macii.cpp (R. Belmont) has them.
 *
 * Physical (32-bit) addresses:
 *   $00000000-$3FFFFFFF  RAM: bank A at 0, bank B after it or where VIA2's
 *                        PA6-PA7 put it (1, 2, 8, 32 MB); the ROM at 0
 *                        while the overlay (VIA1 PA4) is on
 *   $40000000-$4FFFFFFF  ROM, repeated
 *   $50000000-$50FFFFFF  I/O, repeated every MB: VIA1 $0000, VIA2 $2000,
 *                        SCC $4000, SCSI $10000, ASC $14000, IWM $16000,
 *                        VIA1 again at $40000
 *   $Fs000000-$FsFFFFFF  NuBus slot s ($9-$E): the card's space
 * In 24-bit mode (VIA2 PB3 low) the HMMU translates: $000000-$7FFFFF RAM,
 * $800000-$8FFFFF the ROM, $s00000-$sFFFFF the first MB of slot s,
 * $F00000-$FFFFFF the I/O. Unmapped addresses read 0.
 */
#include <string.h>
#include "m68k.h"
#include "mac_internal.h"

#define RAM_PAGES       64          /* 1 MB pages below 64 MB */
#define VRAM_WINDOW     0x100000    /* the 8*24's video RAM */

static struct {
    uint8_t *ram_map[RAM_PAGES];
    bool mode24;                    /* the HMMU translates */
    bool vram_direct;               /* the card's VRAM reads plainly */
    uint8_t ram_bits;               /* VIA2 PA6-PA7 */
    uint8_t slot_irqs;              /* VIA2 PA0-PA5, active low (slot $9 = bit 0) */
    uint64_t next_second;
} ii;

/* ---- Fast-path windows ---- */

static void update_windows(void)
{
    uint32_t pages = 0;
    if (!mac.overlay) {
        while (pages < RAM_PAGES && (pages + 1) * MB <= mac.ram_size &&
               ii.ram_map[pages] == mac.ram + pages * MB) {
            pages++;
        }
    }
    mac.ram_window = pages * MB;
    if (ii.mode24) {
        mac.addr_mask = 0x00FFFFFF;
        mac.rom_base = 0x800000;
        mac.rom_window = mac.overlay ? 0 : MB;
        mac.vram_base = 0x900000;
    } else {
        mac.addr_mask = 0xFFFFFFFF;
        mac.rom_base = 0x40000000;
        mac.rom_window = mac.overlay ? 0 : 0x10000000;
        mac.vram_base = 0xF9000000;
    }
    mac.vram_window = ii.vram_direct ? VRAM_WINDOW : 0;
}

void ii_set_vram_window(bool direct)
{
    ii.vram_direct = direct;
    update_windows();
}

/* ---- RAM banks (VIA2 PA6-PA7: where bank B goes) ---- */

static void map_ram(uint32_t address, uint32_t offset, uint32_t size)
{
    for (uint32_t a = address; a < address + size && a < RAM_PAGES * MB; a += MB) {
        ii.ram_map[a / MB] = mac.ram + offset + (a - address);
    }
}

/* As MAME's macii.cpp, which satisfies the original II's RAM sizing: all
 * of RAM at 0, bank A repeated after it (not with 4 MB), bank B also at
 * its configured place when that's beyond; only 1 MB at 0 when that place
 * is past the end of RAM
 */
static void configure_ram(void)
{
    memset(ii.ram_map, 0, sizeof(ii.ram_map));
    if (!mac.overlay) {
        static const uint32_t places[4] = { 1 * MB, 2 * MB, 8 * MB, 32 * MB };
        uint32_t size = mac.ram_size, bank = size / 2;
        uint32_t b_at = places[ii.ram_bits >> 6];
        if (b_at <= size) {
            map_ram(0, 0, size);
            if (size != 4 * MB) {
                map_ram(size, 0, bank);
            }
            if (b_at >= size + bank) {
                map_ram(b_at, bank, bank);
            }
        } else {
            map_ram(0, 0, MB);
        }
    }
    update_windows();
}

static void set_overlay(bool on)
{
    if (on != mac.overlay) {
        mac.overlay = on;
        if (!on) {
            ii.ram_bits = 0;
        }
        configure_ram();
    }
}

/* ---- I/O and NuBus ---- */

static uint8_t io_read8(uint32_t offset)
{
    uint8_t v;
    switch (offset >> 13) {
    case 0x00: case 0x20: v = via_read(&via1, offset); break;
    case 0x01: v = via_read(&via2, offset); break;
    case 0x02: v = scc_read(offset); break;
    case 0x08: v = scsi_read(offset); break;
    case 0x0A: v = asc_read(offset); break;
    case 0x0B: v = swim_access(offset, false, 0); break;
    default:   v = 0; break;
    }
    IO_LOG(offset, false, v);
    return v;
}

static void io_write8(uint32_t offset, uint8_t v)
{
    IO_LOG(offset, true, v);
    switch (offset >> 13) {
    case 0x00: case 0x20: via_write(&via1, offset, v); break;
    case 0x01: via_write(&via2, offset, v); break;
    case 0x02: scc_write(offset, v); break;
    case 0x08: scsi_write(offset, v); break;
    case 0x0A: asc_write(offset, v); break;
    case 0x0B: swim_access(offset, true, v); break;
    default:   break;
    }
}

enum { R_NONE, R_RAM, R_ROM, R_IO, R_SLOT };

/* Where an address goes: kind, and the offset in it (slot: s << 24 | offset) */
static int decode(uint32_t a, uint32_t *offset)
{
    if (ii.mode24) {
        a &= 0xFFFFFF;
        if (a < 0x800000) {
            *offset = a;
            return mac.overlay ? R_ROM : R_RAM;
        }
        if (a < 0x900000) {
            *offset = a & mac.rom_mask;
            return R_ROM;
        }
        if (a < 0xF00000) {
            *offset = (a >> 20) << 24 | (a & 0xFFFFF);
            return R_SLOT;
        }
        *offset = a & 0xFFFFF;
        return R_IO;
    }
    switch (a >> 28) {
    case 0x0: case 0x1: case 0x2: case 0x3:
        *offset = a;
        return mac.overlay ? R_ROM : R_RAM;
    case 0x4:
        *offset = a & mac.rom_mask;
        return R_ROM;
    case 0x5:
        *offset = a & 0xFFFFF;
        return (a & 0x0F000000) == 0 ? R_IO : R_NONE;
    case 0xF:
        *offset = a & 0x0FFFFFFF;
        return R_SLOT;
    default:
        return R_NONE;
    }
}

static uint8_t slot_read8(uint32_t offset)
{
    return (offset >> 24) == 9 ? jmfb_read8(offset & 0xFFFFFF) : 0;
}

static void slot_write8(uint32_t offset, uint8_t v)
{
    if ((offset >> 24) == 9) {
        jmfb_write8(offset & 0xFFFFFF, v);
    }
}

/* The card's registers take a word or long whole */
static bool slot_register(uint32_t offset)
{
    return (offset >> 24) == 9 && (offset & 0xFFFFFF) >= 0x200000 && (offset & 0xFFFFFF) < 0x200400;
}

static uint8_t read8(uint32_t a)
{
    uint32_t o;
    switch (decode(a, &o)) {
    case R_RAM: {
        const uint8_t *p = o < RAM_PAGES * MB ? ii.ram_map[o >> 20] : NULL;
        return p ? p[(o & (MB - 1)) ^ 1] : 0;
    }
    case R_ROM:  return mac.rom[(o & mac.rom_mask) ^ 1];
    case R_IO:   return io_read8(o);
    case R_SLOT: return slot_read8(o);
    default:     return 0;
    }
}

static void write8(uint32_t a, uint8_t v)
{
    uint32_t o;
    switch (decode(a, &o)) {
    case R_RAM: {
        uint8_t *p = o < RAM_PAGES * MB ? ii.ram_map[o >> 20] : NULL;
        if (p) {
            p[(o & (MB - 1)) ^ 1] = v;
        }
        break;
    }
    case R_IO:   io_write8(o, v); break;
    case R_SLOT: slot_write8(o, v); break;
    default:     break;
    }
}

static uint16_t read16(uint32_t a)
{
    uint16_t hi = read8(a);
    return (hi << 8) | read8((a + 1) & mac.addr_mask);
}

static void write16(uint32_t a, uint16_t v)
{
    uint32_t o;
    if (decode(a, &o) == R_SLOT && slot_register(o)) {
        jmfb_write(o & 0xFFFFFF, v);
        return;
    }
    write8(a, v >> 8);
    write8((a + 1) & mac.addr_mask, v & 0xFF);
}

static void write32(uint32_t a, uint32_t v)
{
    uint32_t o;
    if (decode(a, &o) == R_SLOT && slot_register(o)) {
        jmfb_write(o & 0xFFFFFF, v);
        return;
    }
    write16(a, v >> 16);
    write16((a + 2) & mac.addr_mask, v & 0xFFFF);
}

/* The emulator's access: 24-bit addresses as the HMMU translates them */
static uint8_t *ram_page(uint32_t a)
{
    return a < 0x800000 && !mac.overlay ? ii.ram_map[a >> 20] : NULL;
}

static uint16_t peek16(uint32_t a)
{
    if ((a >= 0x800000 && a < 0x900000) || (mac.overlay && a < 0x800000)) {
        return *(const uint16_t *)(mac.rom + (a & mac.rom_mask));
    }
    if (a >= 0x900000 && a < 0xA00000 && (a & 0xFFFFF) < VRAM_WINDOW && ii.vram_direct) {
        return *(const uint16_t *)(mac.vram + (a & 0xFFFFF));
    }
    return 0;
}

/* ---- VIA1: PA4 overlay, PB0-PB2 the clock, PB3-PB5 ADB ---- */

static uint8_t via1_port_a_inputs(void)
{
    return 0x81;
}

static void via1_port_a_changed(uint8_t old_value, uint8_t value)
{
    (void)old_value;
    set_overlay((value & 0x10) != 0);
}

static uint8_t via1_port_b_inputs(void)
{
    return (rtc_data_out() ? 0x01 : 0) | (adbx_interrupt_line() ? 0x08 : 0);
}

static void via1_port_b_changed(uint8_t old_value, uint8_t value)
{
    if ((old_value ^ value) & 0x30) {
        adbx_state_changed((value >> 4) & 3);
    }
    rtc_lines(value, via1.ddrb);
}

static void via1_irq(bool irq)
{
    mac.irq1 = irq;
    mac_update_irq();
}

static const via_wiring_t via1_wiring = {
    .port_a_inputs = via1_port_a_inputs,
    .port_b_inputs = via1_port_b_inputs,
    .port_a_changed = via1_port_a_changed,
    .port_b_changed = via1_port_b_changed,
    .irq_changed = via1_irq,
};

/* ---- VIA2: PA slot interrupts and RAM banks, PB3 the HMMU, PB7 the tick ---- */

static uint8_t via2_port_a_inputs(void)
{
    return ii.ram_bits | ii.slot_irqs;
}

static void via2_port_a_changed(uint8_t old_value, uint8_t value)
{
    (void)old_value;
    if (!mac.overlay) {
        ii.ram_bits = value & 0xC0;
        configure_ram();
    }
}

static uint8_t via2_port_b_inputs(void)
{
    return 0xCF;
}

static void via2_port_b_changed(uint8_t old_value, uint8_t value)
{
    bool mode24 = !(value & 0x08);
    if (mode24 != ii.mode24) {
        ii.mode24 = mode24;
        update_windows();
    }
    if ((old_value ^ value) & 0x80) {
        via_ca1(&via1, (value & 0x80) != 0);    /* the 60.15 Hz tick */
    }
    if ((old_value & 0x04) && !(value & 0x04) && (via2.ddrb & 0x04)) {
        mac_power_off();
    }
}

static void via2_irq(bool irq)
{
    mac.irq2 = irq;
    mac_update_irq();
}

static const via_wiring_t via2_wiring = {
    .port_a_inputs = via2_port_a_inputs,
    .port_b_inputs = via2_port_b_inputs,
    .port_a_changed = via2_port_a_changed,
    .port_b_changed = via2_port_b_changed,
    .irq_changed = via2_irq,
};

/* A NuBus card's interrupt: VIA2 PA (active low) and CA1, which gets a
 * fresh falling edge for each new one (MAME: an acknowledge can be missed)
 */
void ii_slot_irq(int slot, bool active)
{
    uint8_t mask = 1u << (slot - 9);
    bool was = (ii.slot_irqs & mask) == 0;
    if (active) {
        ii.slot_irqs &= ~mask;
    } else {
        ii.slot_irqs |= mask;
    }
    if ((ii.slot_irqs & 0x3F) != 0x3F) {
        if (active && !was) {
            via_ca1(&via2, true);
        }
        via_ca1(&via2, false);
    } else {
        via_ca1(&via2, true);
    }
}

/* ---- The machine ---- */

static void reset(void)
{
    memset(ii.ram_map, 0, sizeof(ii.ram_map));
    ii.slot_irqs = 0x3F;
    ii.ram_bits = 0;
    ii.mode24 = false;
    ii.vram_direct = true;
    ii.next_second = mac.now + MAC_CPU_HZ;
    mac.overlay = true;
    configure_ram();
    via_reset(&via1);
    via_reset(&via2);
    via_ca1(&via2, true);
    via_cb1(&via2, true);
    adbx_reset();
    rtc_reset();
    jmfb_reset();
    asc_reset(false);
}

static void frame_start(void)
{
    adbx_frame();
    while (mac.now >= ii.next_second) {
        ii.next_second += MAC_CPU_HZ;
        via_set_flag(&via1, VIA_IF_CA2);    /* the clock's one-second interrupt */
    }
}

static void vbl(bool active)
{
    if (active) {
        jmfb_vbl();
    }
}

const model_ops_t ii_ops = {
    .reset = reset,
    .frame_start = frame_start,
    .vbl = vbl,
    .read8 = read8,
    .write8 = write8,
    .read16 = read16,
    .write16 = write16,
    .write32 = write32,
    .ram_page = ram_page,
    .peek16 = peek16,
    .screen_size = jmfb_screen_size,
    .screen_depth = jmfb_screen_depth,
    .render_line = jmfb_render_line,
    .screen_changed = jmfb_screen_changed,
};

void ii_init(void)
{
    mac.rom_mac_base = 0x800000;
    mac.vram_size = VRAM_WINDOW;
    via_init(&via1, &via1_wiring, EV_VIA1_T1, EV_VIA1_T2);
    via_init(&via2, &via2_wiring, EV_VIA2_T1, EV_VIA2_T2);
}
