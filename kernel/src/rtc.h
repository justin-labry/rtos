#ifndef __RTC_H__
#define __RTC_H__

#include <stdint.h>

#define RTC_HOUR(v)	(((v) >> 16) & 0xff)
#define RTC_MINUTE(v)	(((v) >> 8) & 0xff)
#define RTC_SECOND(v)	(((v) >> 0) & 0xff)

#define RTC_YEAR(v)	(((v) >> 24) & 0xff)
#define RTC_MONTH(v)	(((v) >> 16) & 0xff)
#define RTC_DATE(v)	(((v) >> 8) & 0xff)
#define RTC_WEEK(v)	(((v) >> 0) & 0xff)

uint32_t rtc_time();
uint32_t rtc_date();
int rtc_init();

#endif /* __RTC_H__ */
