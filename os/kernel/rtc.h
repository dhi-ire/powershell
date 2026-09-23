#ifndef RTC_H
#define RTC_H

struct datetime { int year, month, day, hour, minute, second; };

void rtc_read(struct datetime *dt);

#endif
