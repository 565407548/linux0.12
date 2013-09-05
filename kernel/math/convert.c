/*
 * linux/kernel/math/convert.c
 *
 * (C) 1991 Linus Torvalds
 */

#include <linux/math_emu.h>

/*
把数据转化为临时高精度数据（tmp_real）
他们的具体格式可以参考 IEEE 754 P548
注意：为了便于比较大小，指数用偏移数表示
 */

/*
 * NOTE!!! There is some "non-obvious" optimisations in the temp_to_long
 * and temp_to_short conversion routines: don't touch them if you don't
 * know what's going on. They are the adding of one in the rounding: the
 * overflow bit is also used for adding one into the exponent. Thus it
 * looks like the overflow would be incorrectly handled, but due to the
 * way the IEEE numbers work, things are correct.
 *
 * There is no checking for total overflow in the conversions, though (ie
 * if the temp-real number simply won't fit in a short- or long-real.)
 */
/*短实型 to 临时实型*/
void short_to_temp(const short_real * a, temp_real * b)
{
	if (!(*a & 0x7fffffff)) {
		b->a = b->b = 0;
		if (*a)
			b->exponent = 0x8000;
		else
			b->exponent = 0;
		return;
	}
        /*
          8位指数形式，其偏移基为：127
          15为指数形式，其偏移基为：16383
          fact= value1-base1
          value2=fact+base2
         */
	b->exponent = ((*a>>23) & 0xff)-127+16383;
	if (*a<0)
		b->exponent |= 0x8000;
	b->b = (*a<<8) | 0x80000000;
	b->a = 0;
}

/*长实型 to 临时实型*/
void long_to_temp(const long_real * a, temp_real * b)
{
	if (!a->a && !(a->b & 0x7fffffff)) {
		b->a = b->b = 0;
		if (a->b)
			b->exponent = 0x8000;
		else
			b->exponent = 0;
		return;
	}
	b->exponent = ((a->b >> 20) & 0x7ff)-1023+16383;
	if (a->b<0)
		b->exponent |= 0x8000;
	b->b = 0x80000000 | (a->b<<11) | (((unsigned long)a->a)>>21);
	b->a = a->a<<11;
}

/*
重点在于两点：
1.有效数字的位1的忽略处理
2.有效数据的舍入问题
*/
void temp_to_short(const temp_real * a, short_real * b)
{
	if (!(a->exponent & 0x7fff)) {
		*b = (a->exponent)?0x80000000:0;
		return;
	}
	*b = ((((long) a->exponent)-16383+127) << 23) & 0x7f800000;
	if (a->exponent < 0)
		*b |= 0x80000000;
	*b |= (a->b >> 8) & 0x007fffff;
	switch (ROUNDING) {
		case ROUND_NEAREST:
			if ((a->b & 0xff) > 0x80)
				++*b;
			break;
		case ROUND_DOWN:
			if ((a->exponent & 0x8000) && (a->b & 0xff))
				++*b;
			break;
		case ROUND_UP:
			if (!(a->exponent & 0x8000) && (a->b & 0xff))
				++*b;
			break;
	}
}

void temp_to_long(const temp_real * a, long_real * b)
{
	if (!(a->exponent & 0x7fff)) {
		b->a = 0;
		b->b = (a->exponent)?0x80000000:0;
		return;
	}
	b->b = (((0x7fff & (long) a->exponent)-16383+1023) << 20) & 0x7ff00000;
	if (a->exponent < 0)
		b->b |= 0x80000000;
	b->b |= (a->b >> 11) & 0x000fffff;
	b->a = a->b << 21;
	b->a |= (a->a >> 11) & 0x001fffff;
	switch (ROUNDING) {
		case ROUND_NEAREST:
			if ((a->a & 0x7ff) > 0x400)
				__asm__("addl $1,%0 ; adcl $0,%1"
					:"=r" (b->a),"=r" (b->b)
					:"0" (b->a),"1" (b->b));
			break;
		case ROUND_DOWN:
			if ((a->exponent & 0x8000) && (a->b & 0xff))
				__asm__("addl $1,%0 ; adcl $0,%1"
					:"=r" (b->a),"=r" (b->b)
					:"0" (b->a),"1" (b->b));
			break;
		case ROUND_UP:
			if (!(a->exponent & 0x8000) && (a->b & 0xff))
				__asm__("addl $1,%0 ; adcl $0,%1"
					:"=r" (b->a),"=r" (b->b)
					:"0" (b->a),"1" (b->b));
			break;
	}
}

/*
临时实数 转化为 临时整数
临时整数也用10字节表示，其中低8字节是无符号整数只，高2字节表示符号位（1负 0正）
 */
void real_to_int(const temp_real * a, temp_int * b)
{
  /*
    fact=value+16383
    value=fact-16383 //实际指数值
    直接把temp_real的有效位复制到temp_int时，需要把指数部分减小63(最高位1已经是整数位),
    value=fact-16383-63
    此值在-64~0才是合适的，否则，表示数据溢出
    value=-value 表示整形数据需要右移的位数

    value<0 表示数据上溢出

    即此时的value值为整形数据指数值，接着通过处理整形数据，把value指数值处理掉
   */
  int shift =  16383 + 63 - (a->exponent & 0x7fff);//需要右移位数
	unsigned long underflow;

	b->a = b->b = underflow = 0;
	b->sign = (a->exponent < 0);
	if (shift < 0) {
		set_OE();
		return;
	}
	if (shift < 32) {
          b->b = a->b; b->a = a->a;/* a->a的部分有效，a->b的全部有效*/
	} else if (shift < 64) {
          b->a = a->b; underflow = a->a;/* a->a的无效，a->b的部分有效*/
		shift -= 32;
	} else if (shift < 96) {
          underflow = a->b;/* a->a，a->b部分都无效*/
		shift -= 64;
	} else
		return;
	__asm__("shrdl %2,%1,%0"
		:"=r" (underflow),"=r" (b->a)
		:"c" ((char) shift),"0" (underflow),"1" (b->a));
	__asm__("shrdl %2,%1,%0"
		:"=r" (b->a),"=r" (b->b)
		:"c" ((char) shift),"0" (b->a),"1" (b->b));
	__asm__("shrl %1,%0"
		:"=r" (b->b)
		:"c" ((char) shift),"0" (b->b));
	switch (ROUNDING) {
		case ROUND_NEAREST:
			__asm__("addl %4,%5 ; adcl $0,%0 ; adcl $0,%1"
				:"=r" (b->a),"=r" (b->b)
				:"0" (b->a),"1" (b->b)
				,"r" (0x7fffffff + (b->a & 1))
				,"m" (*&underflow));
			break;
		case ROUND_UP:
			if (!b->sign && underflow)
				__asm__("addl $1,%0 ; adcl $0,%1"
					:"=r" (b->a),"=r" (b->b)
					:"0" (b->a),"1" (b->b));
			break;
		case ROUND_DOWN:
			if (b->sign && underflow)
				__asm__("addl $1,%0 ; adcl $0,%1"
					:"=r" (b->a),"=r" (b->b)
					:"0" (b->a),"1" (b->b));
			break;
	}
}

void int_to_real(const temp_int * a, temp_real * b)
{
	b->a = a->a;
	b->b = a->b;
	if (b->a || b->b)
		b->exponent = 16383 + 63 + (a->sign? 0x8000:0);
	else {
		b->exponent = 0;
		return;
	}
        /*确保有效数据的最高位为1，即符合规格化数据的要求*/
	while (b->b >= 0) {
		b->exponent--;
		__asm__("addl %0,%0 ; adcl %1,%1"
			:"=r" (b->a),"=r" (b->b)
			:"0" (b->a),"1" (b->b));
	}
}
