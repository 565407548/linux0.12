/*
	本程序重点在与理解P274中图8-4。建立栈环境时，把有和没有错误码的情况分别对待，使二者准备ok的栈格式相同。
	本函数调用的程序，主要分布在：traps.c,system_call.s中
*/
/*
 *  linux/kernel/asm.s
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * asm.s contains the low-level code for most hardware faults.
 * page_exception is handled by the mm, so that isn't here. This
 * file also handles (hopefully) fpu-exceptions due to TS-bit, as
 * the fpu must be properly saved/resored. This hasn't been tested.
 */
	
.globl _divide_error,_debug,_nmi,_int3,_overflow,_bounds,_invalid_op
.globl _double_fault,_coprocessor_segment_overrun
.globl _invalid_TSS,_segment_not_present,_stack_segment
.globl _general_protection,_coprocessor_error,_irq13,_reserved
.globl _alignment_check

_divide_error:
	pushl $_do_divide_error
no_error_code:
	xchgl %eax,(%esp)	#将中断处理函数地址保存与eax中，将eax的值保存于栈中
	pushl %ebx
	pushl %ecx
	pushl %edx
	pushl %edi
	pushl %esi
	pushl %ebp
	push %ds
	push %es
	push %fs
	pushl $0		# "error code"
	lea 44(%esp),%edx	#P274，取eip的地址
	pushl %edx
	movl $0x10,%edx		#内核数据段
	mov %dx,%ds
	mov %dx,%es
	mov %dx,%fs
	call *%eax		#调用中断处理函数，call在执行中断函数前，会把下一个地址先压入栈，然后跳转到中断函数，执行函数
	addl $8,%esp		#P274，esp0,error_code出栈
	pop %fs
	pop %es
	pop %ds
	popl %ebp
	popl %esi
	popl %edi
	popl %edx
	popl %ecx
	popl %ebx
	popl %eax		#至中断返回的情景，栈顶依次为：eip,cs,eflags,esp,ss
	iret

_debug:
	pushl $_do_int3		# _do_debug
	jmp no_error_code

_nmi:
	pushl $_do_nmi
	jmp no_error_code

_int3:
	pushl $_do_int3
	jmp no_error_code

_overflow:
	pushl $_do_overflow
	jmp no_error_code

_bounds:
	pushl $_do_bounds
	jmp no_error_code

_invalid_op:
	pushl $_do_invalid_op
	jmp no_error_code

_coprocessor_segment_overrun:
	pushl $_do_coprocessor_segment_overrun
	jmp no_error_code

_reserved:
	pushl $_do_reserved
	jmp no_error_code

#数学协处理器硬件中断（P163）。由协处理器引发异常，通过8259A将异常告知CPU
#0xF0是协处理器端口，用于清忙锁存器。通过写该端口，可以消除CPU的BUSY延续信号，并重新激活80387的处理器扩展请求引脚PEREO。该操作主要是确保在继续执行80387的任何指令之前，CPU响应本中断
_irq13:
	pushl %eax
	xorb %al,%al
	outb %al,$0xF0
	movb $0x20,%al			#向8259主中断控制器发送EOI信号
	outb %al,$0x20
	jmp 1f				#延时
1:	jmp 1f	
1:	outb %al,$0xA0			#向8259从片发送EOI信号
	popl %eax
	jmp _coprocessor_error

_double_fault:
	pushl $_do_double_fault
error_code:
	xchgl %eax,4(%esp)		# error code <-> %eax
	xchgl %ebx,(%esp)		# &function <-> %ebx
	pushl %ecx
	pushl %edx
	pushl %edi
	pushl %esi
	pushl %ebp
	push %ds
	push %es
	push %fs
	pushl %eax			# error code
	lea 44(%esp),%eax		# offset
	pushl %eax			#eip地址
	movl $0x10,%eax			#内核数据段
	mov %ax,%ds
	mov %ax,%es
	mov %ax,%fs
	call *%ebx			#执行中断处理程序
	addl $8,%esp			#esp0,error_code出栈
	pop %fs
	pop %es
	pop %ds
	popl %ebp
	popl %esi
	popl %edi
	popl %edx
	popl %ecx
	popl %ebx
	popl %eax
	iret				#至中断返回的情景，栈顶依次为：eip,cs,eflags,esp,ss	

_invalid_TSS:
	pushl $_do_invalid_TSS
	jmp error_code

_segment_not_present:
	pushl $_do_segment_not_present
	jmp error_code

_stack_segment:
	pushl $_do_stack_segment
	jmp error_code

_general_protection:
	pushl $_do_general_protection
	jmp error_code

_alignment_check:
	pushl $_do_alignment_check
	jmp error_code

