/*
 * Shared state of the Macintosh. Not part of the public API.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "mac.h"

#ifdef ESP_PLATFORM
#include "esp_attr.h"
#include "esp_log.h"
#define MAC_HOT             IRAM_ATTR
#define MAC_LOGI(...)       ESP_LOGI("mac", __VA_ARGS__)
#define MAC_LOGW(...)       ESP_LOGW("mac", __VA_ARGS__)
#else
#include <stdio.h>
#define MAC_HOT
#define MAC_LOGI(fmt, ...)  printf("I mac: " fmt "\n", ##__VA_ARGS__)
#define MAC_LOGW(fmt, ...)  printf("W mac: " fmt "\n", ##__VA_ARGS__)
#endif

/* 31.3344 MHz / 2. Time is kept in "lines" of 704 CPU cycles, one sound
 * sample each (22.255 kHz), 370 to a 60.15 Hz frame: the tick that goes to
 * VIA1 CA1. The monitor's vertical blanking (the last 45 of its 525 lines)
 * is the same share of the frame.
 */
#define MAC_CPU_HZ          15667200
#define LINE_CYCLES         704
#define FRAME_LINES         370
#define FRAME_CYCLES        (LINE_CYCLES * FRAME_LINES)
#define VBL_LINE            338         /* vertical blanking from here to the end */
#define VIA_DIVIDER         20          /* VIA clock: CPU / 20 */

/* Address map (24 bits; the V8 ignores A30-A24 and in 24-bit mode A31) */
#define ROM_BASE            0xA00000
#define VRAM_BASE           0xF40000
#define RAM_PAGES           10          /* 1 MB pages, $000000-$9FFFFF */
#define BOARD_RAM           0x200000    /* the LC's own 2 MB */

/* Low memory globals */
#define LM_TICKS            0x016A
#define LM_MTEMP            0x0828
#define LM_RAWMOUSE         0x082C
#define LM_MOUSE            0x0830
#define LM_CRSRNEW          0x08CE
#define LM_CRSRCOUPLE       0x08CF
#define LM_JIODONE          0x08FC

/* Scheduled events (cycle times) */
enum {
    EV_VIA_T1,
    EV_VIA_T2,
    EV_EGRET,           /* Egret acts on the handshake lines */
    EV_ASC,             /* the sound FIFO drains */
    EV_COUNT,
};

#define EV_NEVER            UINT64_MAX

typedef struct {
    mac_model_t model;

    /* Memory. RAM, ROM and VRAM are stored with each 16-bit word in host
     * order, so a word read is a plain load and byte n lives at index n ^ 1.
     * RAM: the SIMMs first, then the 2 MB on the board, so that the usual
     * configuration (SIMMs at 0, the board's RAM after them) maps address a
     * to ram[a].
     */
    uint8_t *ram;
    uint32_t ram_size;
    uint32_t simm_size;
    uint8_t *ram_map[RAM_PAGES];    /* host memory of each 1 MB page, NULL = none */
    uint32_t ram_window;            /* addresses below it are ram[a] (fast path) */
    uint8_t *vram;
    bool vram_written;
    uint8_t *rom;
    uint32_t rom_size;
    uint32_t rom_mask;
    bool overlay;                   /* ROM at 0 (power-on) */
    uint32_t addr_mask;             /* 24-bit mode $00FFFFFF, 32-bit $80FFFFFF */
    uint32_t driver_start;          /* the replacement .Sony driver in the ROM */
    uint32_t driver_end;            /* (offsets, native calls only there) */
    mac_monitor_t monitor;

    /* Time */
    uint64_t now;                   /* CPU cycles, as of the last sync */
    uint64_t frame_start;
    bool cpu_running;               /* inside m68k_execute() */
    int slice;                      /* machine time until the CPU stops */
    uint64_t events[EV_COUNT];

    uint32_t frames;
    bool halted;

    /* Interrupt lines */
    bool via_irq;                   /* level 1 */
    bool pvia_irq;                  /* level 2 */
    bool scc_irq;                   /* level 4 */
    bool nmi;

    uint8_t *pram;
    void (*pram_changed)(void);
    bool pram_dirty;
    void (*disk_ejected)(int drive);
} mac_machine_t;

extern mac_machine_t mac;

/* mac.c */
uint64_t mac_now(void);
void mac_schedule(int event, uint64_t when);
void mac_update_irq(void);
void mac_restart(void);             /* Egret's reset: at the end of the frame */

/* mem.c */
void mem_reset(void);
void mem_set_overlay(bool on);
void mem_set_ram_config(uint8_t config);
void mem_set_24bit(bool on);
/* Memory access for the emulator itself (no side effects) */
uint8_t mac_peek8(uint32_t address);
uint32_t mac_peek32(uint32_t address);
void mac_poke8(uint32_t address, uint8_t value);
void mac_poke16(uint32_t address, uint16_t value);
void mac_poke32(uint32_t address, uint32_t value);
bool mac_ram_range(uint32_t address, uint32_t len);
void mac_ram_write(uint32_t address, const uint8_t *src, uint32_t len);
void mac_ram_read(uint32_t address, uint8_t *dst, uint32_t len);
uint32_t mac_ram_top(void);         /* end of RAM as the Mac sees it */

/* via.c: VIA1 */
void via_reset(void);
uint8_t via_read(uint32_t address);
void via_write(uint32_t address, uint8_t value);
void via_timer_event(int event);
void via_set_flag(int flag);        /* VIA_IF_* */
uint8_t via_port_a(void);           /* output levels as the Mac set them */
uint8_t via_port_b(void);
/* Shift register, as the external device (Egret) */
bool via_sr_shifting_in(void);      /* ACR: shift in under external clock */
bool via_sr_shifting_out(void);     /* ACR: shift out under external clock */
void via_sr_receive(uint8_t value); /* device -> Mac: SR full, IRQ */
uint8_t via_sr_send(void);          /* Mac -> device: SR emptied, IRQ */

#define VIA_IF_CA2      0x01
#define VIA_IF_CA1      0x02        /* the 60.15 Hz tick */
#define VIA_IF_SR       0x04        /* Egret data */
#define VIA_IF_CB2      0x08
#define VIA_IF_CB1      0x10
#define VIA_IF_T2       0x20
#define VIA_IF_T1       0x40

/* Port bits */
#define PA_HEAD_SEL     0x20
#define PB_EGRET_XCVR   0x08        /* in: transceiver session, active low (TREQ) */
#define PB_EGRET_FULL   0x10        /* out: VIA full, active high (TACK) */
#define PB_EGRET_SYS    0x20        /* out: system session, active high (TIP) */

/* Called by the VIA */
void via_port_b_changed(uint8_t old_value, uint8_t value);
uint8_t via_port_a_inputs(void);
uint8_t via_port_b_inputs(void);
void via_sr_written(void);          /* Mac wrote the shift register */
void via_acr_changed(uint8_t old_acr, uint8_t acr);

/* pvia.c: the pseudo-VIA (slot and sound interrupts, RAM and video config) */
void pvia_reset(void);
uint8_t pvia_read(uint32_t address);
void pvia_write(uint32_t address, uint8_t value);
void pvia_vbl(bool active);
void pvia_asc_irq(bool active);
uint8_t pvia_video_config(void);    /* register $10: bits 0-2 depth */

/* egret.c */
void egret_reset(void);
void egret_lines(bool sys_session, bool via_full);
bool egret_xcvr_line(void);         /* PB3 level: false = session (asserted) */
void egret_event(void);
void egret_frame(void);
uint32_t egret_mac_time(void);

/* adb.c: ADB keyboard (address 2) and mouse (3), behind Egret */
void adb_reset(void);
/* An ADB command (aaaa cc rr) with listen data: the talk reply in out
 * (up to 8 bytes) and its length; -1 when no device answered
 */
int adb_command(uint8_t command, const uint8_t *data, int len, uint8_t *out);
/* Autopoll: the device with something to report, its talk command and data */
int adb_poll(uint16_t devices, uint8_t *command, uint8_t *out);
bool adb_mouse_active(void);

/* input.c: events from other tasks */
enum { INPUT_KEY, INPUT_MOUSE, INPUT_MOUSE_ABS };
typedef struct {
    uint8_t type;
    uint8_t code;                   /* key code */
    uint8_t value;                  /* key down / mouse button */
    int16_t dx, dy;                 /* motion, or position */
} input_event_t;
bool input_next_key(uint8_t *code, bool *down);
bool input_key_waiting(void);
void input_reset(void);
void mouse_frame(void);             /* once a frame: take input, move the pointer */
bool mouse_button(void);
int adb_mouse_delta(int *dx, int *dy);      /* motion owed to the ADB mouse */
void adb_mouse_took(int dx, int dy);        /* ... and the part it reported */

/* video.c: Ariel colour table and the picture */
void video_reset(void);
uint8_t ariel_read(uint32_t address);
void ariel_write(uint32_t address, uint8_t value);
void video_mode_changed(void);

/* asc.c: the V8's Apple Sound Chip (FIFO A only) */
void asc_reset(void);
uint8_t asc_read(uint32_t address);
void asc_write(uint32_t address, uint8_t value);
void asc_event(void);
void asc_frame_end(void);

/* chips.c */
void scc_reset(void);
uint8_t scc_read(uint32_t address);
void scc_write(uint32_t address, uint8_t value);
void swim_reset(void);
uint8_t swim_access(uint32_t address, bool write, uint8_t value);
void scsi_reset(void);
uint8_t scsi_read(uint32_t address);
void scsi_write(uint32_t address, uint8_t value);

/* sony.c: the disk driver */
void sony_reset(void);
bool sony_install(uint8_t *rom, uint32_t rom_size, uint32_t offset);
int sony_native(uint16_t function);
void sony_frame(void);

/* rom.c */
bool rom_prepare(const uint8_t *image, uint32_t size);
