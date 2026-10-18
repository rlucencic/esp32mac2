/*
 * Macintosh LC: reset, the frame loop with its event scheduler, interrupts
 * and how VIA1's port lines are wired.
 *
 * Time is counted in CPU cycles. The CPU runs until the next scheduled
 * event (VIA timers, Egret, the sound FIFO) or the next frame landmark
 * (vertical blanking, the end of the frame).
 *
 * Interrupts (V8): VIA1 on level 1, the pseudo-VIA (slots, sound) on 2,
 * the SCC on 4, the interrupt switch on 7; the highest one wins.
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "m68k.h"
#include "mac_internal.h"
#include "emudbg.h"

mac_machine_t mac;

static bool s_reset_pending;

/* The LC's 68020 reaches RAM, ROM and video RAM over a 16-bit bus: every
 * long word, instruction fetches included, is two bus cycles of 3 clocks
 * or more. Musashi's timings are the 68020's cache case on a 32-bit bus.
 * Each of its cycles counts as CPU_SCALE / 256 cycles of the machine's
 * time: about a real LC's speed (Musashi's own timing ran the idle Finder
 * at 2.5 million instructions a second, more than the Tab5 keeps up with).
 */
#define CPU_SCALE       512

static inline uint64_t cpu_to_time(uint64_t cycles)
{
    return cycles * CPU_SCALE / 256;
}

uint64_t mac_now(void)
{
    return mac.cpu_running ? mac.now + cpu_to_time(m68k_cycles_run()) : mac.now;
}

void mac_schedule(int event, uint64_t when)
{
    mac.events[event] = when;
    /* Sooner than the CPU was going to stop: stop it there */
    if (mac.cpu_running && when < mac.now + mac.slice) {
        m68k_end_timeslice();
    }
}

void mac_update_irq(void)
{
    int level = mac.nmi ? 7 : mac.scc_irq ? 4 : mac.pvia_irq ? 2 : mac.via_irq ? 1 : 0;
    m68k_set_irq(level);
}

void mac_restart(void)
{
    s_reset_pending = true;
    if (mac.cpu_running) {
        m68k_end_timeslice();
    }
}

mac_model_t mac_model(void)
{
    return mac.model;
}

/* ---- VIA1 port wiring ---- */

/* PA: the machine's identification (and diagnostics off) */
uint8_t via_port_a_inputs(void)
{
    return 0xD5;
}

uint8_t via_port_b_inputs(void)
{
    return egret_xcvr_line() ? PB_EGRET_XCVR : 0;
}

void via_port_b_changed(uint8_t old_value, uint8_t value)
{
    if ((old_value ^ value) & (PB_EGRET_FULL | PB_EGRET_SYS)) {
        egret_lines((value & PB_EGRET_SYS) != 0, (value & PB_EGRET_FULL) != 0);
    }
}

/* ---- Musashi hooks ---- */

static int int_ack(int level)
{
    (void)level;
    return M68K_INT_ACK_AUTOVECTOR;
}

/* Native calls from the replacement disk driver: ILLEGAL ($4AFC) followed
 * by a function word, only inside the driver in the ROM
 */
#define NATIVE_OPCODE   0x4AFC

int m68k_host_illegal(int opcode)
{
    if (opcode != NATIVE_OPCODE) {
        return 0;
    }
    uint32_t pc = m68k_get_reg(NULL, M68K_REG_PC);
    uint32_t at = (pc - 2) & 0xFFFFFF;
    bool in_rom = (at & 0xF00000) == ROM_BASE || (mac.overlay && at < ROM_BASE);
    uint32_t offset = at & mac.rom_mask;
    if (!in_rom || offset < mac.driver_start || offset >= mac.driver_end) {
        return 0;
    }
    uint16_t function = mac_peek16(pc);
    m68k_set_reg(M68K_REG_PC, pc + 2);
    int result = sony_native(function);
    m68k_set_reg(M68K_REG_D0, (uint32_t)result);
    return 1;
}

void m68k_host_halted(void)
{
    if (!mac.halted) {
        mac.halted = true;
        MAC_LOGW("CPU halted (double bus/address fault) at PC $%08lx",
                 (unsigned long)m68k_get_reg(NULL, M68K_REG_PPC));
    }
}

void m68k_host_exception(unsigned int vector)
{
#ifndef ESP_PLATFORM
    /* Host build: bus and address errors */
    static int logged;
    if ((vector == 2 || vector == 3 || vector == 11) && logged < 16) {
        logged++;
        uint32_t ppc = m68k_get_reg(NULL, M68K_REG_PPC);
        MAC_LOGW("%s, PC $%08lx (opcode $%04x $%04x)", vector == 2 ? "bus error" : vector == 3 ? "address error" : "line F",
                 (unsigned long)ppc, mac_peek16(ppc), mac_peek16(ppc + 2));
    }
#else
    (void)vector;
#endif
}

/* ---- Running ---- */

static void fire_events(void)
{
    for (int ev = 0; ev < EV_COUNT; ev++) {
        if (mac.events[ev] > mac.now) {
            continue;
        }
        switch (ev) {
        case EV_VIA_T1:
        case EV_VIA_T2:
            via_timer_event(ev);
            break;
        case EV_EGRET:
            mac.events[ev] = EV_NEVER;
            egret_event();
            break;
        case EV_ASC:
            mac.events[ev] = EV_NEVER;
            asc_event();
            break;
        }
    }
}

static void run_until(uint64_t target)
{
    while (mac.now < target) {
        if (mac.halted || s_reset_pending) {
            mac.now = target;
            break;
        }
        uint64_t next = target;
        for (int ev = 0; ev < EV_COUNT; ev++) {
            if (mac.events[ev] < next) {
                next = mac.events[ev];
            }
        }
        if (next > mac.now) {
            mac.slice = (int)(next - mac.now);
            int cycles = (int)(((uint64_t)mac.slice * 256 + CPU_SCALE - 1) / CPU_SCALE);
            mac.cpu_running = true;
            int ran = m68k_execute(cycles);
            mac.cpu_running = false;
            mac.now += cpu_to_time(ran > 0 ? ran : 1);
        }
        fire_events();
    }
}

void mac_run_frame(void)
{
    if (s_reset_pending) {
        mac_reset();
    }
    mouse_frame();
    egret_frame();
    via_set_flag(VIA_IF_CA1);           /* the 60.15 Hz tick */
    pvia_vbl(false);
    run_until(mac.frame_start + (uint64_t)VBL_LINE * LINE_CYCLES);
    pvia_vbl(true);
    run_until(mac.frame_start + FRAME_CYCLES);

    asc_frame_end();
    sony_frame();
    mac.frame_start += FRAME_CYCLES;
    mac.frames++;
    if (mac.pram_dirty) {
        mac.pram_dirty = false;
        if (mac.pram_changed) {
            mac.pram_changed();
        }
    }
}

void mac_interrupt_switch(void)
{
    mac.nmi = true;
    mac_update_irq();
    mac.nmi = false;        /* the CPU latches level 7 at the next instruction */
}

void mac_reset(void)
{
    s_reset_pending = false;
    for (int ev = 0; ev < EV_COUNT; ev++) {
        mac.events[ev] = EV_NEVER;
    }
    mac.frame_start = mac.now;
    mac.halted = false;
    mac.via_irq = mac.pvia_irq = mac.scc_irq = mac.nmi = false;
    memset(mac.ram, 0, mac.ram_size);
    memset(mac.vram, 0, MAC_VRAM_SIZE);

    mem_reset();
    via_reset();
    pvia_reset();
    egret_reset();
    adb_reset();
    input_reset();
    video_reset();
    asc_reset();
    scc_reset();
    swim_reset();
    scsi_reset();
    sony_reset();
    mac_update_irq();
    m68k_pulse_reset();
}

/* ---- The debugger (emudbg.h) ---- */

static uint8_t dbg_peek(void *ctx, uint32_t a)
{
    (void)ctx;
    return mac_peek8(a);
}

static void dbg_poke(void *ctx, uint32_t a, uint8_t v)
{
    (void)ctx;
    mac_poke8(a, v);
}

static emudbg_cpu_t s_dbg_cpus[] = {
    { .name = "68020", .arch = &emudbg_arch_68000, .peek = dbg_peek, .poke = dbg_poke },
};

static void dbg_where(char *buf, size_t len)
{
    snprintf(buf, len, "frame %lu, cycle %lu", (unsigned long)mac.frames, (unsigned long)(mac.now - mac.frame_start));
}

static void dbg_screen(int *width, int *height)
{
    mac_screen_size(width, height);
}

static emudbg_machine_t s_dbg = {
    .name = "Macintosh",
    .cpus = s_dbg_cpus,
    .cpu_count = 1,
    .where = dbg_where,
    .reset = mac_reset,
    .screen = dbg_screen,
    .render_line = mac_render_line_rgb565,
};

bool mac_init(const mac_config_t *config)
{
    memset(&mac, 0, sizeof(mac));
    const char *name = mac_rom_identify(config->rom, config->rom_size, &mac.model);
    if (name == NULL || config->rom_copy == NULL) {
        MAC_LOGW("not a ROM this emulator knows (checksum %02x%02x%02x%02x)",
                 config->rom[0], config->rom[1], config->rom[2], config->rom[3]);
        return false;
    }
    uint32_t ram = config->ram_size;
    if (ram != 2 * 0x100000 && ram != 4 * 0x100000 && ram != 6 * 0x100000 && ram != 10 * 0x100000) {
        MAC_LOGW("RAM size %lu: needs 2, 4, 6 or 10 MB", (unsigned long)ram);
        return false;
    }
    if (config->vram == NULL) {
        return false;
    }
    mac.ram = config->ram;
    mac.ram_size = ram;
    mac.simm_size = ram - BOARD_RAM;
    mac.vram = config->vram;
    mac.rom = config->rom_copy;
    mac.rom_size = config->rom_size;
    mac.rom_mask = config->rom_size - 1;
    mac.monitor = config->monitor;
    mac.pram = config->pram;
    if (mac.pram[0x10] != 0xA8) {
        mac_pram_defaults(mac.pram, mac.model);
        mac.pram_dirty = true;
    }
    mac.pram_changed = config->pram_changed;
    mac.disk_ejected = config->disk_ejected;

    if (!rom_prepare(config->rom, config->rom_size)) {
        return false;
    }
    MAC_LOGI("%s, %lu MB RAM", name, (unsigned long)(ram >> 20));

    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68020);
    if (config->opcode_tables != NULL) {
        m68k_use_table_copies(config->opcode_tables);
    }
    m68k_set_int_ack_callback(int_ack);
    mac_reset();
    s_dbg.name = name;
    emudbg_set_machine(&s_dbg);
    return true;
}

void mac_get_status(mac_status_t *status)
{
    status->frames = mac.frames;
    status->pc = m68k_get_reg(NULL, M68K_REG_PC);
    status->sr = m68k_get_reg(NULL, M68K_REG_SR);
    status->ticks = mac.overlay ? 0 : mac_peek32(LM_TICKS);
    status->halted = mac.halted;
}
