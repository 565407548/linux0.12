/*
 * linux/kernel/math/compare.c
 *
 * (C) 1991 Linus Torvalds
 */

/*
 * temporary real comparison routines
 */

#include <linux/math_emu.h>

/*c3=1 c2=1 c1=0 c0=1*/
#define clear_Cx() (I387.swd &= ~0x4500)

/*规格化处理*/
static void normalize(temp_real * a)
{
	int i = a->exponent & 0x7fff;
	int sign = a->exponent & 0x8000;

	if (!(a->a || a->b)) {
		a->exponent = 0;
		return;
	}
	while (i && a->b >= 0) {/* 通过a->b正负来判定其最高位是否为1，为1则表示是规格化数据 */
		i--;
		__asm__("addl %0,%0 ; adcl %1,%1"
			:"=r" (a->a),"=r" (a->b)
			:"0" (a->a),"1" (a->b));
	}
	a->exponent = i | sign;
}

void ftst(const temp_real * a)
{
	temp_real b;

	clear_Cx();
	b = *a;
	normalize(&b);
	if (b.a || b.b || b.exponent) {
		if (b.exponent < 0)
                  set_C0();//负数
	} else
          set_C3();//0
}

void fcom(const temp_real * src1, const temp_real * src2)
{
	temp_real a;

	a = *src1;
	a.exponent ^= 0x8000;
	fadd(&a,src2,&a);// src1-src2
	ftst(&a);
}

/*无次序比较*/
void fucom(const temp_real * src1, const temp_real * src2)
{
	fcom(src1,src2);
}
