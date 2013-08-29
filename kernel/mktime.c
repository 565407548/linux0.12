









/*
 *  linux/kernel/mktime.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <time.h>

/*
 * This isn't the library routine, it is only used in the kernel.
 * as such, we don't care about years<1970 etc, but assume everything
 * is ok. Similarly, TZ etc is happily ignored. We just do everything
 * as easily as possible. Let's find something public for the library
 * routines (although I think minix times is public).
 */
/*
 * PS. I hate whoever though up the year 1970 - couldn't they have gotten
 * a leap-year instead? I also hate Gregorius, pope or no. I'm grumpy.
 */
/* 单位为：s */
#define MINUTE 60
#define HOUR (60*MINUTE)
#define DAY (24*HOUR)
#define YEAR (365*DAY)

/* interestingly, we assume leap-years */
static int month[12] = {
	0,
	DAY*(31),
	DAY*(31+29),
	DAY*(31+29+31),
	DAY*(31+29+31+30),
	DAY*(31+29+31+30+31),
	DAY*(31+29+31+30+31+30),
	DAY*(31+29+31+30+31+30+31),
	DAY*(31+29+31+30+31+30+31+31),
	DAY*(31+29+31+30+31+30+31+31+30),
	DAY*(31+29+31+30+31+30+31+31+30+31),
	DAY*(31+29+31+30+31+30+31+31+30+31+30)
};

/*
leap year： (x%4==0 && x%100!=0) || (x%400==0)
只考虑1970年以后的时间
以下函数在处理2000年有问题，2000不是leap year，而以下函数把其当作leap year处理
*/
long kernel_mktime(struct tm * tm)
{
	long res;
	int year;
/*1972为leap year,
实际年份和year对照表
1970 0
1971 1
1972 2
1973 3
...
2012 42
...
即 year=2(实际1972) 对应 leap year
每4年一个leap year
可通过(year+2) % 4 == 0 判断是否为leap year
*/
	year = tm->tm_year - 70;
/* magic offsets (y+1) needed to get leapyears right.*/
/*
1970（包含1970）年到 x 年经过了多少个leap year？
1973（3）、1977（7）、1981（11）分别经过了1、2、3个leap year
即：x年经过的leap year 为： (x+1)/4
*/
	res = YEAR*year + DAY*((year+1)/4);
	
	res += month[tm->tm_mon];
/* and (y+2) here. If it wasn't a leap-year, we have to adjust
根据本年度是否为leap year 做适当调整
 */
	if (tm->tm_mon>1 && ((year+2)%4))
		res -= DAY;
	res += DAY*(tm->tm_mday-1);
	res += HOUR*tm->tm_hour;
	res += MINUTE*tm->tm_min;
	res += tm->tm_sec;
	return res;
}
