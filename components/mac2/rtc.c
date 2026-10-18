/*
 * The Mac II's real-time clock and parameter RAM (from ../esp32mac), on
 * VIA1 port B: PB0 data (both ways),
 * PB1 clock, PB2 /enable. Bits move on the rising clock edge, most
 * significant first. A command byte is
 *
 *   r aaaaa 01    r = 1 read; a < 8 seconds byte a & 3 (little endian),
 *                 8-11 PRAM $08-$0B, 12 test, 13 write-protect,
 *                 16-31 PRAM $10-$1F
 *   r 0111 sss    extended (XPRAM): a second byte x aaaaa xx gives
 *                 address sss aaaaa of the 256 bytes
 *
 * then for writes a data byte follows; for reads the RTC sends one back.
 *
 * The seconds follow the host's local time; a time the Mac sets is kept as
 * an offset from it.
 */
#include <string.h>
#include "mac_internal.h"

static struct {
    bool enabled;
    bool clock;
    bool line;              /* data line level */
    bool sending;           /* this byte goes out to the Mac */
    bool send_next;         /* ... from the next clock on */
    uint8_t shift;
    uint8_t count;          /* bits left in the byte (mod 8) */
    uint8_t mode;           /* 0 command, 1 write data, 2 extended address, 3 extended data */
    uint8_t command;
    uint8_t address;
    bool write_protect;
    int64_t offset;         /* Mac time minus host time, seconds */
} rtc;

#define PB_RTC_DATA     0x01
#define PB_RTC_CLOCK    0x02
#define PB_RTC_ENABLE   0x04        /* active low */

static uint32_t mac_time(void)
{
    return (uint32_t)(host_mac_time() + rtc.offset);
}

void rtc_reset(void)
{
    int64_t offset = rtc.offset;
    memset(&rtc, 0, sizeof(rtc));
    rtc.offset = offset;
    rtc.line = true;
}

static uint8_t *pram_byte(uint8_t address)
{
    return &mac.pram[address];
}

static void pram_write(uint8_t address, uint8_t value)
{
    if (!rtc.write_protect && *pram_byte(address) != value) {
        *pram_byte(address) = value;
        mac.pram_dirty = true;
    }
}

/* Classic register access: returns the byte read (writes return 0) */
static uint8_t access(uint8_t command, bool write, uint8_t data)
{
    uint8_t a = (command >> 2) & 0x1F;
    if (a < 8) {
        int i = a & 3;
        if (write) {
            if (!rtc.write_protect) {
                uint32_t t = mac_time();
                t = (t & ~(0xFFu << (8 * i))) | ((uint32_t)data << (8 * i));
                rtc.offset = (int64_t)t - host_mac_time();
            }
            return 0;
        }
        return (mac_time() >> (8 * i)) & 0xFF;
    }
    if (a < 12) {
        if (write) {
            pram_write(0x08 + (a & 3), data);
            return 0;
        }
        return *pram_byte(0x08 + (a & 3));
    }
    if (a < 16) {
        if (write && a == 13) {
            rtc.write_protect = (data & 0x80) != 0;
        }
        return 0;
    }
    if (write) {
        pram_write(0x10 + (a & 0x0F), data);
        return 0;
    }
    return *pram_byte(0x10 + (a & 0x0F));
}

static void byte_done(void)
{
    uint8_t b = rtc.shift;
    switch (rtc.mode) {
    case 0:
        if ((b & 0x78) == 0x38) {
            rtc.command = b;
            rtc.mode = 2;
        } else if (b & 0x80) {
            rtc.shift = access(b, false, 0);
            rtc.send_next = true;
        } else {
            rtc.command = b;
            rtc.mode = 1;
        }
        break;
    case 1:
        access(rtc.command, true, b);
        rtc.mode = 0;
        break;
    case 2:
        rtc.address = ((rtc.command & 7) << 5) | ((b >> 2) & 0x1F);
        if (rtc.command & 0x80) {
            rtc.shift = *pram_byte(rtc.address);
            rtc.send_next = true;
            rtc.mode = 0;
        } else {
            rtc.mode = 3;
        }
        break;
    case 3:
        pram_write(rtc.address, b);
        rtc.mode = 0;
        break;
    }
}

void rtc_lines(uint8_t port_b, uint8_t ddr_b)
{
    /* /enable driven low by the Mac */
    bool enabled = (ddr_b & PB_RTC_ENABLE) && !(port_b & PB_RTC_ENABLE);
    bool clock = (port_b & PB_RTC_CLOCK) && (ddr_b & PB_RTC_CLOCK);
    if (ddr_b & PB_RTC_DATA) {
        rtc.line = (port_b & PB_RTC_DATA) != 0;
    }

    if (!enabled) {
        if (rtc.enabled) {
            /* Abort whatever was going on */
            rtc.mode = 0;
            rtc.sending = rtc.send_next = false;
            rtc.shift = 0;
            rtc.count = 0;
        }
    } else if (clock && !rtc.clock) {
        rtc.sending = rtc.send_next;
        rtc.count = (rtc.count - 1) & 7;
        if (rtc.sending) {
            rtc.line = (rtc.shift >> rtc.count) & 1;
            if (rtc.count == 0) {
                rtc.send_next = false;
            }
        } else {
            rtc.shift = (rtc.shift << 1) | (rtc.line ? 1 : 0);
            if (rtc.count == 0) {
                byte_done();
            }
        }
    }
    rtc.enabled = enabled;
    rtc.clock = clock;
}

bool rtc_data_out(void)
{
    return rtc.line;
}
