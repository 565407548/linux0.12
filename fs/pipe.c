/*
 *  linux/fs/pipe.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <signal.h>
#include <errno.h>
#include <termios.h>

#include <linux/sched.h>
#include <linux/mm.h>	/* for get_free_page */
#include <asm/segment.h>
#include <linux/kernel.h>

/*
管道作为一种特殊文件，在内存实现为：
1.有文件inode
2.文件inode对应一个高速缓存块（PAGE_SIZE）

管道头处写数据，管道尾处读数据
*/


int read_pipe(struct m_inode * inode, char * buf, int count)
{
	int chars, size, read = 0;

	while (count>0) {
		while (!(size=PIPE_SIZE(*inode))) {
			wake_up(& PIPE_WRITE_WAIT(*inode));
			if (inode->i_count != 2) /* are there any writers? */
				return read;
			if (current->signal & ~current->blocked)
				return read?read:-ERESTARTSYS;
			interruptible_sleep_on(& PIPE_READ_WAIT(*inode));
		}
                /*
                  管道有效数据情况示意图：
                  情况1：
                       tail      head
                  |    -----------    |
                  情况2：
                  |-----         -----|
                       head      tail
                */
		chars = PAGE_SIZE-PIPE_TAIL(*inode);
		if (chars > count)
			chars = count;
		if (chars > size)
			chars = size;
		count -= chars;
		read += chars;
		size = PIPE_TAIL(*inode);//开始读取数据处，有效数据偏移处
		PIPE_TAIL(*inode) += chars;
		PIPE_TAIL(*inode) &= (PAGE_SIZE-1);
		while (chars-->0)
			put_fs_byte(((char *)inode->i_size)[size++],buf++);
	}
	wake_up(& PIPE_WRITE_WAIT(*inode));
	return read;
}
	
int write_pipe(struct m_inode * inode, char * buf, int count)
{
	int chars, size, written = 0;

	while (count>0) {
		while (!(size=(PAGE_SIZE-1)-PIPE_SIZE(*inode))) {
			wake_up(& PIPE_READ_WAIT(*inode));
			if (inode->i_count != 2) { /* no readers */
				current->signal |= (1<<(SIGPIPE-1));
				return written?written:-1;
			}
			sleep_on(& PIPE_WRITE_WAIT(*inode));
		}
                /*
                  管道有效数据情况示意图：
                  情况1：
                  tail      head
                  |    -----------    |
                  情况2：
                  |-----         -----|
                  head      tail
                */
		chars = PAGE_SIZE-PIPE_HEAD(*inode);
		if (chars > count)
			chars = count;
		if (chars > size)//size空闲空间
			chars = size;
		count -= chars;
		written += chars;
		size = PIPE_HEAD(*inode);
		PIPE_HEAD(*inode) += chars;
		PIPE_HEAD(*inode) &= (PAGE_SIZE-1);
		while (chars-->0)
			((char *)inode->i_size)[size++]=get_fs_byte(buf++);
	}
	wake_up(& PIPE_READ_WAIT(*inode));
	return written;
}


/*
管道系统调用的内核实现：
1.管道读写文件描述表（file_table中两项）
2.进程管道描述符（filp两项）
3.获得inode

返回管道读写文件描述符
*/
int sys_pipe(unsigned long * fildes)
{
	struct m_inode * inode;
	struct file * f[2];
	int fd[2];
	int i,j;

	j=0;
	for(i=0;j<2 && i<NR_FILE;i++)
		if (!file_table[i].f_count)
			(f[j++]=i+file_table)->f_count++;
	if (j==1)
		f[0]->f_count=0;
	if (j<2)
		return -1;
        
	j=0;
	for(i=0;j<2 && i<NR_OPEN;i++)
		if (!current->filp[i]) {
			current->filp[ fd[j]=i ] = f[j];
			j++;
		}
	if (j==1)
		current->filp[fd[0]]=NULL;
	if (j<2) {
		f[0]->f_count=f[1]->f_count=0;
		return -1;
	}
        
	if (!(inode=get_pipe_inode())) {
		current->filp[fd[0]] =
			current->filp[fd[1]] = NULL;
		f[0]->f_count = f[1]->f_count = 0;
		return -1;
	}
	f[0]->f_inode = f[1]->f_inode = inode;
	f[0]->f_pos = f[1]->f_pos = 0;
	f[0]->f_mode = 1;		/* read */
	f[1]->f_mode = 2;		/* write */
	put_fs_long(fd[0],0+fildes);
	put_fs_long(fd[1],1+fildes);
	return 0;
}


int pipe_ioctl(struct m_inode *pino, int cmd, int arg)
{
	switch (cmd) {
          case FIONREAD:/*读取管道有效数据长*/
			verify_area((void *) arg,4);
			put_fs_long(PIPE_SIZE(*pino),(unsigned long *) arg);
			return 0;
		default:
			return -EINVAL;
	}
}
