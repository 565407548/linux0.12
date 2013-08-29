/*
 *  linux/kernel/signal.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/segment.h>

#include <signal.h>/**/
#include <errno.h>
  
int sys_sgetmask()
{
	return current->blocked;
}

/*
SIGKILL 和 SIGSTOP 不能被屏蔽
*/
int sys_ssetmask(int newmask)
{
	int old=current->blocked;

	current->blocked = newmask & ~(1<<(SIGKILL-1)) & ~(1<<(SIGSTOP-1));
	return old;
}

int sys_sigpending(sigset_t *set)
{
    /* fill in "set" with signals pending but blocked. */
    verify_area(set,4);
    /*把还未处理，却被阻塞的信号保存于set所指向地址处*/
    put_fs_long(current->blocked & current->signal, (unsigned long *)set);
    return 0;
}

/* atomically swap in the new signal mask, and wait for a signal.
 *
 * we need to play some games with syscall restarting.  We get help
 * from the syscall library interface.  Note that we need to coordinate
 * the calling convention with the libc routine.
 *
 * "set" is just the sigmask as described in 1003.1-1988, 3.3.7.
 * 	It is assumed that sigset_t can be passed as a 32 bit quantity.
 *
 * "restart" holds a restart indication.  If it's non-zero, then we 
 * 	install the old mask, and return normally.  If it's zero, we store 
 * 	the current mask in old_mask and block until a signal comes in.
 */
/*
该系统调用会临时把进程信号的屏蔽码替换成参数中给定的set，然后挂起，直到收到一个信号为止
当系统第一次运行时，restart=0，跳过if语句，执行后面的语句；当再次执行时，会执行if语句体
*/
int sys_sigsuspend(int restart, unsigned long old_mask, unsigned long set)
{
    extern int sys_pause(void);

    if (restart) {
	/* we're restarting */
	current->blocked = old_mask;
	return -EINTR;
    }
    /* we're not restarting.  do the work */
    *(&restart) = 1;
    *(&old_mask) = current->blocked;
    current->blocked = set;
    (void) sys_pause();			/* return after a signal arrives */
    return -ERESTARTNOINTR;		/* handle the signal, and come back */
}

/*
内核态运行时：fs指向用户态数据段；cs执行内核态数据段
*/
static inline void save_old(char * from,char * to)
{
	int i;

	verify_area(to, sizeof(struct sigaction));
	for (i=0 ; i< sizeof(struct sigaction) ; i++) {
		put_fs_byte(*from,to);
		from++;
		to++;
	}
}

static inline void get_new(char * from,char * to)
{
	int i;

	for (i=0 ; i< sizeof(struct sigaction) ; i++)
		*(to++) = get_fs_byte(from++);
}

/*
signal,不安全信号处理函数
 */
int sys_signal(int signum, long handler, long restorer)
{
	struct sigaction tmp;

	if (signum<1 || signum>32 || signum==SIGKILL || signum==SIGSTOP)
		return -EINVAL;
	tmp.sa_handler = (void (*)(int)) handler;/*函数指针*/
	tmp.sa_mask = 0;
	tmp.sa_flags = SA_ONESHOT | SA_NOMASK;
	tmp.sa_restorer = (void (*)(void)) restorer;
	handler = (long) current->sigaction[signum-1].sa_handler;
	
	current->sigaction[signum-1] = tmp;

	return handler;/*返回原处理函数指针*/
}

/*
sigaction，安全信号处理函数
 */
int sys_sigaction(int signum, const struct sigaction * action,
	struct sigaction * oldaction)
{
	struct sigaction tmp;

	if (signum<1 || signum>32 || signum==SIGKILL || signum==SIGSTOP)
		return -EINVAL;
	tmp = current->sigaction[signum-1];
        /*get_new(char *from ,char *to)*/
	get_new((char *) action,
		(char *) (signum-1+current->sigaction));
	if (oldaction)
		save_old((char *) &tmp,(char *) oldaction);
	if (current->sigaction[signum-1].sa_flags & SA_NOMASK)
		current->sigaction[signum-1].sa_mask = 0;
	else
		current->sigaction[signum-1].sa_mask |= (1<<(signum-1));
	return 0;
}

/*
 * Routine writes a core dump image in the current directory.
 * Currently not implemented.
 */
int core_dump(long signr)
{
	return(0);	/* We didn't do a dump */
}

/*
P321
 */
int do_signal(long signr,long eax,long ebx, long ecx, long edx, long orig_eax,
	long fs, long es, long ds,
	long eip, long cs, long eflags,
	unsigned long * esp, long ss)
{
	unsigned long sa_handler;
	long old_eip=eip;
	struct sigaction * sa = current->sigaction + signr - 1;
	int longs;

	unsigned long * tmp_esp;

#ifdef notdef
	printk("pid: %d, signr: %x, eax=%d, oeax = %d, int=%d\n", 
		current->pid, signr, eax, orig_eax, 
		sa->sa_flags & SA_INTERRUPT);
#endif
        /*orig_eax=-1表示不是系统调用中断*/
        /*如果是在系统调用终端用的本函数，并且相应系统调用的返回码eax等于-ERESTARTSYS或-ERESTARTNOINTR时进行下面处理（实际还没有真正返回用户程序中）*/
	if ((orig_eax != -1) &&
	    ((eax == -ERESTARTSYS) || (eax == -ERESTARTNOINTR))) {
		if ((eax == -ERESTARTSYS) && ((sa->sa_flags & SA_INTERRUPT) ||
		    signr < SIGCONT || signr > SIGTTOU))
			*(&eax) = -EINTR;
		else {
			*(&eax) = orig_eax;
			*(&eip) = old_eip -= 2;/*一条指令占用2字节，重新执行系统调用*/
		}
	}
	sa_handler = (unsigned long) sa->sa_handler;
        //P793
	if (sa_handler==1)//#define SIG_IGN 1
		return(1);   /* Ignore, see if there are more signals... */
	if (!sa_handler) {//#define SIG_DEF 0
		switch (signr) {
		case SIGCONT:
		case SIGCHLD:
			return(1);  /* Ignore, ... */

		case SIGSTOP:
		case SIGTSTP:
		case SIGTTIN:
		case SIGTTOU:
			current->state = TASK_STOPPED;
			current->exit_code = signr;
			if (!(current->p_pptr->sigaction[SIGCHLD-1].sa_flags & 
					SA_NOCLDSTOP))
				current->p_pptr->signal |= (1<<(SIGCHLD-1));
			return(1);  /* Reschedule another event */

		case SIGQUIT:
		case SIGILL:
		case SIGTRAP:
		case SIGIOT:
		case SIGFPE:
		case SIGSEGV:
			if (core_dump(signr))
				do_exit(signr|0x80);
			/* fall through */
		default:
			do_exit(signr);
		}
	}
	/*
	 * OK, we're invoking a handler
	 */
        /*
          1.修改内核栈中返回用户栈的eip，指向信号处理函数
          2.在用户栈中构建信号处理函数所需要的环境，主要是栈环境。中断返回时，信号处理函数执行需要的环境要求
         */
	if (sa->sa_flags & SA_ONESHOT)
		sa->sa_handler = NULL;
	*(&eip) = sa_handler;
	longs = (sa->sa_flags & SA_NOMASK)?7:8;
	*(&esp) -= longs;
	verify_area(esp,longs*4);
	tmp_esp=esp;
	put_fs_long((long) sa->sa_restorer,tmp_esp++);
	put_fs_long(signr,tmp_esp++);
	if (!(sa->sa_flags & SA_NOMASK))
		put_fs_long(current->blocked,tmp_esp++);
	put_fs_long(eax,tmp_esp++);
	put_fs_long(ecx,tmp_esp++);
	put_fs_long(edx,tmp_esp++);
	put_fs_long(eflags,tmp_esp++);
	put_fs_long(old_eip,tmp_esp++);
	current->blocked |= sa->sa_mask;
	return(0);		/* Continue, execute handler */
}
