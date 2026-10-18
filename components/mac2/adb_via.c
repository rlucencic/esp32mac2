/*
 * The Mac II's ADB transceiver behind VIA1's shift register (from
 * ../esp32mac's SE and Classic), in front of the devices in adb.c.
 *
 * The Mac drives the transceiver state on PB4/PB5 (ST0/ST1):
 *   0  start a command: the command byte is in SR
 *   1  even data byte, 2 odd data byte (talk: device -> SR; listen: SR -> device)
 *   3  idle: the transceiver polls the last device by itself
 * and sees PB3 (/ADB interrupt) go low when a talk had no data (time-out).
 *
 * The state sequence follows Mini vMac's, which the SE and Classic ROMs
 * are known to be happy with.
 */
#include <string.h>
#include "mac_internal.h"

#define STATE_CYCLES    10880       /* ~0.7 ms after a state change */
#define MAX_DATA        8

static struct {
    uint8_t state;
    uint8_t command;
    bool listening;
    bool talk_data;
    uint8_t data[MAX_DATA];
    uint8_t size, index;
    bool int_line;                  /* PB3: high = nothing to report */
} ax;

void adbx_reset(void)
{
    memset(&ax, 0, sizeof(ax));
    ax.int_line = true;
}

bool adbx_interrupt_line(void)
{
    return ax.int_line;
}

void adbx_state_changed(uint8_t st)
{
    ax.state = st;
    mac_schedule(EV_ADB, mac_now() + STATE_CYCLES);
}

static void do_talk(void)
{
    uint8_t out[MAX_DATA];
    int n = adb_command(ax.command, NULL, 0, out);
    if (n > 0) {
        memcpy(ax.data, out, n);
        ax.size = n;
        ax.talk_data = true;
    }
}

void adbx_event(void)
{
    ax.int_line = true;
    switch (ax.state) {
    case 0:
        if (ax.listening) {
            ax.listening = false;
            ax.size = ax.index;
            uint8_t out[MAX_DATA];
            adb_command(ax.command, ax.data, ax.size, out);
        }
        ax.talk_data = false;
        ax.index = 0;
        ax.size = 0;
        ax.command = via_sr_send(&via1);
        switch ((ax.command >> 2) & 3) {
        case 0: {
            uint8_t out[MAX_DATA];
            adb_command(ax.command, NULL, 0, out);
            if ((ax.command & 3) == 1) {
                ax.data[0] = ax.data[1] = 0;    /* flush */
                ax.size = 2;
                ax.talk_data = true;
            }
            break;
        }
        case 2:
            ax.listening = true;
            break;
        case 3:
            do_talk();
            break;
        }
        break;

    case 1:
    case 2:
        if (ax.listening) {
            uint8_t b = via_sr_send(&via1);
            if (ax.index < MAX_DATA) {
                ax.data[ax.index++] = b;
            }
        } else if (!ax.talk_data || ax.index >= ax.size) {
            via_sr_receive(&via1, 0xFF);
            ax.int_line = false;        /* time-out: no (more) data */
        } else {
            via_sr_receive(&via1, ax.data[ax.index++]);
        }
        break;

    case 3:
        if (ax.talk_data) {
            via_sr_receive(&via1, 0xFF);
        } else if (adb_anything_to_report()) {
            if (((ax.command >> 2) & 3) == 3) {
                do_talk();
            }
            via_sr_receive(&via1, 0xFF);
        }
        break;
    }
}

/* Once a frame: idle with something new to report gets the Mac's attention */
void adbx_frame(void)
{
    if (ax.state == 3 && !ax.talk_data && adb_anything_to_report()) {
        if (((ax.command >> 2) & 3) == 3) {
            do_talk();
        }
        via_sr_receive(&via1, 0xFF);
    }
}
