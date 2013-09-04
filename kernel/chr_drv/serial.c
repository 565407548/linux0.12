/*
 *  linux/kernel/serial.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 *	serial.c
 *
 * This module implements the rs232 io functions
 *	void rs_write(struct tty_struct * queue);
 *	void rs_init(void);
 * and all interrupts pertaining to serial IO.
 */

#include <linux/tty.h>
#include <linux/sched.h>
#include <asm/system.h>
#include <asm/io.h>

/*

UART编程过程（P515）
1.设置传输波特率
2.设置通行方式（主要指明有无奇偶校验位、几位数据、有无停止位等）
3.设置modem控制寄存器 (反馈方式、中断方式、查询方式)
4.初始化中断允许寄存器（可以发生多种中断设置允许那些中断。如无数据发送时，不允许引发”发送保持寄存器空中断“）

本内核采用的是中断方式，因此在进行了相关初始化后，我们需要处理的任务就是等待中断发生，中断通过8259A中断控制器发生。
当有中断时，先通过 中断标识寄存器 来判定是何种中断，然后进入对应的中断处理程序，进行实际中断处理。
这部分主要是 rs_io.s 的任务；本文件主要是进行初始话处理

 */

/* 写队列中含有 1024/4个字符时，就开始发送  */
#define WAKEUP_CHARS (TTY_BUF_SIZE/4)

extern void rs1_interrupt(void);
extern void rs2_interrupt(void);

static void init(int port)
{
	outb_p(0x80,port+3);	/* set DLAB of line control reg */
	outb_p(0x30,port);	/* LS of divisor (48 -> 2400 bps */
	outb_p(0x00,port+1);	/* MS of divisor */
	outb_p(0x03,port+3);	/* reset DLAB */
	outb_p(0x0b,port+4);	/* set DTR,RTS, OUT_2 */
	outb_p(0x0d,port+1);	/* enable all intrs but writes */
	(void)inb(port);	/* read data port to reset things (?) */
}

void rs_init(void)
{
	set_intr_gate(0x24,rs1_interrupt);
	set_intr_gate(0x23,rs2_interrupt);
	init(tty_table[64].read_q->data);
	init(tty_table[65].read_q->data);
	outb(inb_p(0x21)&0xE7,0x21);
}

/*
 * This routine gets called when tty_write has put something into
 * the write_queue. It must check wheter the queue is empty, and
 * set the interrupt register accordingly
 *
 *	void _rs_write(struct tty_struct * tty);
 */
void rs_write(struct tty_struct * tty)
{
	cli();
	if (!EMPTY(tty->write_q))
          outb(inb_p(tty->write_q->data+1)|0x02,tty->write_q->data+1);//允许 发送寄存器空中断， 即允许发送数据
	sti();
}
