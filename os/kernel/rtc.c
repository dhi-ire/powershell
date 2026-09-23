/* CMOS real-time clock. */
#include "rtc.h"
#include "io.h"

static uint8_t cmos(uint8_t reg) { outb(0x70, reg); return inb(0x71); }
static int bcd(int v) { return (v & 0x0F) + (v >> 4) * 10; }

void rtc_read(struct datetime *dt)
{
    while (cmos(0x0A) & 0x80) ;      /* wait for update to finish */
    int s = cmos(0x00), m = cmos(0x02), h = cmos(0x04);
    int d = cmos(0x07), mo = cmos(0x08), y = cmos(0x09);
    uint8_t fmt = cmos(0x0B);

    if (!(fmt & 0x04)) {             /* BCD mode */
        s = bcd(s); m = bcd(m); d = bcd(d); mo = bcd(mo); y = bcd(y);
        h = bcd(h & 0x7F) | (h & 0x80);
    }
    if (!(fmt & 0x02) && (h & 0x80)) /* 12-hour clock, PM */
        h = ((h & 0x7F) + 12) % 24;

    dt->second = s; dt->minute = m; dt->hour = h & 0x7F;
    dt->day = d; dt->month = mo; dt->year = 2000 + y;
}
