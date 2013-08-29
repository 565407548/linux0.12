/*
 *  linux/kernel/system_call.s
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 *  system_call.s  contains the system-call low-level handling routines.
 * This also contains the timer-interrupt handler, as some of the code is
 * the same. The hd- and flopppy-interrupts are also here.
 *
 * NOTE: This code handles signal-recognition, which happens every time
 * after a timer-interrupt and after each system call. Ordinary interrupts
 * don't handle signal-recognition, as that would clutter them up totally
 * unnecessarily.
 *
 * Stack layout in 'ret_from_system_call':
 *
 *	 0(%esp) - %eax
 *	 4(%esp) - %ebx
 *	 8(%esp) - %ecx
 *	 C(%esp) - %edx
 *	10(%esp) - original %eax	(-1 if not system call)
 *	14(%esp) - %fs
 *	18(%esp) - %es
 *	1C(%esp) - %ds
 *	20(%esp) - %eip
 *	24(%esp) - %cs
 *	28(%esp) - %eflags
 *	2C(%esp) - %oldesp
 *	30(%esp) - %oldss
 */

SIG_CHLD	= 17

EAX		= 0x00
EBX		= 0x04
ECX		= 0x08
EDX		= 0x0C
ORIG_EAX	= 0x10
FS		= 0x14
ES		= 0x18
DS		= 0x1C
EIP		= 0x20
CS		= 0x24
EFLAGS		= 0x28
OLDESP		= 0x2C
OLDSS		= 0x30

/*include/linux/sched.h*/
state	= 0		# these are offsets into the task-struct.
counter	= 4
priority = 8
signal	= 12
sigaction = 16		# MUST be 16 (=len of sigaction)
blocked = (33*16)

# offsets within sigaction, include/signal.h
sa_handler = 0
sa_mask = 4
sa_flags = 8
sa_restorer = 12

nr_system_calls = 82 /*系统调用总数*/

ENOSYS = 38	/*系统调用出错码*/

/*
 * Ok, I get parallel printer interrupts while using the floppy for some
 * strange reason. Urgel. Now I just ignore them.
 */
.globl _system_call,_sys_fork,_timer_interrupt,_sys_execve
.globl _hd_interrupt,_floppy_interrupt,_parallel_interrupt
.globl _device_not_available, _coprocessor_error

.align 2
bad_sys_call:
	pushl $-ENOSYS
	jmp ret_from_sys_call
.align 2
reschedule:
	pushl $ret_from_sys_call
	jmp _schedule

#系统调用	
.align 2
_system_call:
	push %ds
	push %es
	push %fs
	pushl %eax		# save the orig_eax
	pushl %edx		
	pushl %ecx		# push %ebx,%ecx,%edx as parameters
	pushl %ebx		# to the system call
	movl $0x10,%edx		# set up ds,es to kernel space
	mov %dx,%ds
	mov %dx,%es
	movl $0x17,%edx		# fs points to local data space
	mov %dx,%fs
	cmpl _NR_syscalls,%eax
	jae bad_sys_call
	call _sys_call_table(,%eax,4) #调用指定功能号的C函数,include/linux/sys.h
	pushl %eax
2:
	movl _current,%eax		#_current为当前任务数据结构指针，即task_struct指针
	cmpl $0,state(%eax)		# state,TASK_RUNNING=0
	jne reschedule			# 程序处于不可运行状态
	cmpl $0,counter(%eax)		# counter
	je reschedule			# 时间片用完
ret_from_sys_call:
	movl _current,%eax		#_current为当前任务数据结构指针，即task_struct指针
	cmpl _task,%eax			# _task指向task[0] cannot have signals
	je 3f
	cmpw $0x0f,CS(%esp)		# was old code segment supervisor ?
	jne 3f				#非用户代码段
	cmpw $0x17,OLDSS(%esp)		# was stack segment = 0x17 ?
	jne 3f				#非用户数据段
	movl signal(%eax),%ebx		#取信号位图->ebx
	movl blocked(%eax),%ecx
	notl %ecx
	andl %ebx,%ecx
	bsfl %ecx,%ecx			#从低位开始扫描ecx（从0-31），如果存在不为0的位，则把偏移存于ecx中
	je 3f				#没有则跳转
	btrl %ecx,%ebx			#复位在ebx中偏移为ecx的位
	movl %ebx,signal(%eax)		#保存新的signal
	incl %ecx
	pushl %ecx
	call _do_signal			#kernel/signal.c
	popl %ecx
	testl %eax, %eax
	jne 2b		# see if we need to switch tasks, or do more signals
3:	popl %eax
	popl %ebx
	popl %ecx
	popl %edx
	addl $4, %esp	# skip orig_eax
	pop %fs
	pop %es
	pop %ds
	iret
	
#软中断，协处理器错误
.align 2
_coprocessor_error:
	push %ds
	push %es
	push %fs
	pushl $-1		# fill in -1 for orig_eax，非系统调用标志
	pushl %edx
	pushl %ecx
	pushl %ebx
	pushl %eax
	movl $0x10,%eax		#内核数据段
	mov %ax,%ds
	mov %ax,%es
	movl $0x17,%eax		#用户数据段
	mov %ax,%fs
	pushl $ret_from_sys_call
	jmp _math_error

#int 16
#kernel/math/error.c
.align 2
_device_not_available:
	push %ds
	push %es
	push %fs
	pushl $-1		# fill in -1 for orig_eax，非系统调用标志
	pushl %edx
	pushl %ecx
	pushl %ebx
	pushl %eax
	movl $0x10,%eax		#内核数据段
	mov %ax,%ds
	mov %ax,%es
	movl $0x17,%eax		#用户数据段
	mov %ax,%fs
	pushl $ret_from_sys_call
	clts				# clear TS(task state) so that we can use math
	movl %cr0,%eax
	testl $0x4,%eax			# EM (math emulation bit)
	je _math_state_restore		#模拟标志为为0
	pushl %ebp
	pushl %esi
	pushl %edi
	pushl $0		# temporary storage for ORIG_EIP
	call _math_emulate	#数学协处理器模拟函数,math/math_emulate.c
	addl $4,%esp
	popl %edi
	popl %esi
	popl %ebp
	ret

#int 32
# include/linux/sched.c
.align 2
_timer_interrupt:
	push %ds		# save ds,es and put kernel data space
	push %es		# into them. %fs is used by _system_call
	push %fs
	pushl $-1		# fill in -1 for orig_eax，非系统调用标志
	pushl %edx		# we save %eax,%ecx,%edx as gcc doesn't
	pushl %ecx		# save those across function calls. %ebx
	pushl %ebx		# is saved as we use that in ret_sys_call
	pushl %eax
	movl $0x10,%eax		#内核数据段
	mov %ax,%ds
	mov %ax,%es
	movl $0x17,%eax		#用户数据段
	mov %ax,%fs
	incl _jiffies
	movb $0x20,%al		# EOI to interrupt controller #1
	outb %al,$0x20
	movl CS(%esp),%eax	#CS=24，栈中保存的cs数据，发生中断时被压入栈的
	andl $3,%eax		# %eax is CPL (0 or 3, 0=supervisor)
	pushl %eax
	call _do_timer		# 'do_timer(long CPL)' does everything from
	addl $4,%esp		# task switching to accounting ...函数内会进行进程切换 kernel/sched.c P309
	jmp ret_from_sys_call

.align 2
_sys_execve:
	lea EIP(%esp),%eax
	pushl %eax
	call _do_execve		#fs/execve.c
	addl $4,%esp
	ret

.align 2
_sys_fork:
	call _find_empty_process	#kernel/fork.c, 为新进程取得进程号 last_pid
	testl %eax,%eax
	js 1f
	push %gs
	pushl %esi
	pushl %edi
	pushl %ebp
	pushl %eax
	call _copy_process		#kernel/fork.c
	addl $20,%esp
1:	ret

#int 46
_hd_interrupt:
	pushl %eax
	pushl %ecx
	pushl %edx
	push %ds
	push %es
	push %fs
	movl $0x10,%eax
	mov %ax,%ds
	mov %ax,%es
	movl $0x17,%eax
	mov %ax,%fs
	movb $0x20,%al
	outb %al,$0xA0		# EOI to interrupt controller #1，往从片发送EOI
	jmp 1f			# give port chance to breathe
1:	jmp 1f
1:	xorl %edx,%edx
	movl %edx,_hd_timeout	#_hd_timeout=0,表示已经在规定时间内产生了中断
	xchgl _do_hd,%edx	#_do_hd可能为：read_intr,write_intr或unexpected_hd_interrupt。_do_hd保存到edx后，令_do_hd=null
	testl %edx,%edx		#判断_do_hd是否为空
	jne 1f
	movl $_unexpected_hd_interrupt,%edx
1:	outb %al,$0x20		#往主片发送EOI
	call *%edx		# "interesting" way of handling intr.
	pop %fs
	pop %es
	pop %ds
	popl %edx
	popl %ecx
	popl %eax
	iret

#int 38
_floppy_interrupt:
	pushl %eax
	pushl %ecx
	pushl %edx
	push %ds
	push %es
	push %fs
	movl $0x10,%eax
	mov %ax,%ds
	mov %ax,%es
	movl $0x17,%eax
	mov %ax,%fs
	movb $0x20,%al
	outb %al,$0x20		# EOI to interrupt controller #1
	xorl %eax,%eax
	xchgl _do_floppy,%eax
	testl %eax,%eax
	jne 1f
	movl $_unexpected_floppy_interrupt,%eax
1:	call *%eax		# "interesting" way of handling intr.
	pop %fs
	pop %es
	pop %ds
	popl %edx
	popl %ecx
	popl %eax
	iret

_parallel_interrupt:
	pushl %eax
	movb $0x20,%al
	outb %al,$0x20
	popl %eax
	iret
