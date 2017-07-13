#include "stdio.h"
#include "asm.h"
#include "port.h"
#include "rtc.h"

#include <util/cmd.h>


#define RTC_ADDRESS		0x70
#define RTC_DATA		0x71

#define RTC_ADDR_SECOND		0x00
#define RTC_ADDR_MINUTE		0x02
#define RTC_ADDR_HOUR		0x04

#define RTC_ADDR_WEEK		0x06
#define RTC_ADDR_DATE		0x07
#define RTC_ADDR_MONTH		0x08
#define RTC_ADDR_YEAR		0x09

#define BCD(v)	(((((v) >> 4) & 0x0f) * 10) + ((v) & 0x0f))


static const char* months[] = {
	"???", "Jan",  "Feb", "Mar", "Apr", "May", "Jun", "Jul",  "Aug", "Sep", "Oct", "Nov", "Dec",
};
static const char* weeks[] = {
	"???", "Sun", "Mon",  "Tue", "Wed", "Thu", "Fri", "Sat",
};

// 0xff0000: hour, 0xff00: minute, 0xff: second
uint32_t rtc_time() {
	uint32_t result = 0;
	uint32_t tmp;
	
	port_out8(RTC_ADDRESS, RTC_ADDR_HOUR);
	tmp = port_in8(RTC_DATA);
	result |= BCD(tmp) << 16;
	
	port_out8(RTC_ADDRESS, RTC_ADDR_MINUTE);
	tmp = port_in8(RTC_DATA);
	result |= BCD(tmp) << 8;
	
	port_out8(RTC_ADDRESS, RTC_ADDR_SECOND);
	tmp = port_in8(RTC_DATA);
	result |= BCD(tmp) << 0;
	
	return result;
}

// 0xff000000: year, 0xff0000: month, 0xff00: day of month, 0xff: day of week
uint32_t rtc_date() {
	uint32_t result = 0;
	uint32_t tmp;
	
	port_out8(RTC_ADDRESS, RTC_ADDR_YEAR);
	tmp = port_in8(RTC_DATA);
	result |= BCD(tmp) << 24;
	
	port_out8(RTC_ADDRESS, RTC_ADDR_MONTH);
	tmp = port_in8(RTC_DATA);
	result |= BCD(tmp) << 16;
	
	port_out8(RTC_ADDRESS, RTC_ADDR_DATE);
	tmp = port_in8(RTC_DATA);
	result |= BCD(tmp) << 8;
	
	port_out8(RTC_ADDRESS, RTC_ADDR_WEEK);
	tmp = port_in8(RTC_DATA);
	result |= BCD(tmp) << 0;
	
	return result;
}

static int cmd_date(int argc, char** argv, void(*callback)(char* result, int exit_status)) {
	uint32_t date = rtc_date();
	uint32_t time = rtc_time();

	printf("%s %s %d %02d:%02d:%02d UTC %d\n",
			weeks[RTC_WEEK(date)], months[RTC_MONTH(date)], RTC_DATE(date),
			RTC_HOUR(time), RTC_MINUTE(time), RTC_SECOND(time), 2000 + RTC_YEAR(date));

	return 0;
}

static Command commands[] = {
	{
		.name = "date",
		.desc = "Print current date and time.",
		.func = cmd_date
	},
};

int rtc_init() {
	cmd_register(commands, sizeof(commands) / sizeof(commands[0]));
	return 0;
}
