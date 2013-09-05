/*
 * linux/kernel/math/ea.c
 *
 * (C) 1991 Linus Torvalds
 */

/*
 * Calculate the effective address.
 */

#include <stddef.h>

#include <linux/math_emu.h>
#include <asm/segment.h>

/*
ea:effective address
本文件主要是获取操作数的有效偏移地址
原理还是通过进一步分析指令来解析指令，及其后面字节数据的意义，获得正确的指令地址
具体可参考：P567
 */

/*
offsetof 在 include/stddef.h 中定义
#define offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)
 */
static int __regoffset[] = {
	offsetof(struct info,___eax),
	offsetof(struct info,___ecx),
	offsetof(struct info,___edx),
	offsetof(struct info,___ebx),
	offsetof(struct info,___esp),
	offsetof(struct info,___ebp),
	offsetof(struct info,___esi),
	offsetof(struct info,___edi)
};

/* 取保存在 struct info 结构体中的寄存器数据*/
#define REG(x) (*(long *)(__regoffset[(x)]+(char *) info))

/*
两字节寻址模式中第2操作数指示字节SIB（scale,Index,Base） P567,P568
 */
static char * sib(struct info * info, int mod)
{
	unsigned char ss,index,base;
	long offset = 0;

        //从用户空间取出SIB字节
	base = get_fs_byte((char *) EIP);
	EIP++;
	ss = base >> 6;
	index = (base >> 3) & 7;
	base &= 7;
        //索引代码为0x100表示没有索引
	if (index == 4)
		offset = 0;
	else
		offset = REG(index);
        //索引偏移值=对因寄存器内容*比例因子
	offset <<= ss;
	if (mod || base != 5)
		offset += REG(base);
	if (mod == 1) {
		offset += (signed char) get_fs_byte((char *) EIP);
		EIP++;
	} else if (mod == 2 || base == 5) {
		offset += (signed) get_fs_long((unsigned long *) EIP);
		EIP += 4;
	}
	I387.foo = offset;
	I387.fos = 0x17;
	return (char *) offset;
}

/*
根据指令中的寻址模式字节计算有效地址值
 */
char * ea(struct info * info, unsigned short code)
{
	unsigned char mod,rm;
	long * tmp = &EAX;
	int offset = 0;

	mod = (code >> 6) & 3;
	rm = code & 7;
	if (rm == 4 && mod != 3)
		return sib(info,mod);
	if (rm == 5 && !mod) {
		offset = get_fs_long((unsigned long *) EIP);
		EIP += 4;
		I387.foo = offset;
		I387.fos = 0x17;
		return (char *) offset;
	}
	tmp = & REG(rm);
	switch (mod) {
		case 0: offset = 0; break;
		case 1:
			offset = (signed char) get_fs_byte((char *) EIP);
			EIP++;
			break;
		case 2:
			offset = (signed) get_fs_long((unsigned long *) EIP);
			EIP += 4;
			break;
		case 3:
			math_abort(info,1<<(SIGILL-1));
	}
	I387.foo = offset;
	I387.fos = 0x17;
	return offset + (char *) *tmp;
}
