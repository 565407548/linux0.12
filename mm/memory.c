/*
 *  linux/mm/memory.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * demand-loading started 01.12.91 - seems it is high on the list of
 * things wanted, and it should be easy to implement. - Linus
 */

/*
 * Ok, demand-loading was easy, shared pages a little bit tricker. Shared
 * pages started 02.12.91, seems to work. - Linus.
 *
 * Tested sharing by executing about 30 /bin/sh: under the old kernel it
 * would have taken more than the 6M I have free, but it worked well as
 * far as I could see.
 *
 * Also corrected some "invalidate()"s - I wasn't doing enough of them.
 */

/*
 * Real VM (paging to/from disk) started 18.12.91. Much more work and
 * thought has to go into this. Oh, well..
 * 19.12.91  -  works, somewhat. Sometimes I get faults, don't know why.
 *		Found it. Everything seems to work now.
 * 20.12.91  -  Ok, making the swap-device changeable like the root.
 */

#include <signal.h>

#include <asm/system.h>

#include <linux/sched.h>
#include <linux/head.h>
#include <linux/kernel.h>

/*
已经存在的内存映射数据发生改变时，就需要刷新页高速缓冲，即调用 invalidate()
 */

/*???*/
#define CODE_SPACE(addr) ((((addr)+4095)&~4095) < \
current->start_code + current->end_code)

unsigned long HIGH_MEMORY = 0;

/*
  页面复制，参数from,to均为物理地址
  movsl：[esi]->[edi]
*/
#define copy_page(from,to) \
__asm__("cld ; rep ; movsl"::"S" (from),"D" (to),"c" (1024):"cx","di","si")


unsigned char mem_map [ PAGING_PAGES ] = {0,};

/*
 * Free a page of memory at physical address 'addr'. Used by
 * 'free_page_tables()'
 */
/*
主内存区地址：LOW_MEM~HIGH_MEM
addr:物理地址
 */
void free_page(unsigned long addr)
{
	if (addr < LOW_MEM) return;
	if (addr >= HIGH_MEMORY)
		panic("trying to free nonexistent page");
	addr -= LOW_MEM;
	addr >>= 12;
	if (mem_map[addr]--) return;
	mem_map[addr]=0;
	panic("trying to free free page");
}

/*
 * This function frees a continuos block of page tables, as needed
 * by 'exit()'. As does copy_page_tables(), this handles only 4Mb blocks.
 */
/*
释放4M内存，由一张页表对应的内存空间
from:线性地址，释放的内存的线性空间首地址，必须是4M的倍数
size：释放内存空间大小，字节为单位。需要先处理成删除页目录项个数（包括页目录项对应页表及对应内存页）
 */
int free_page_tables(unsigned long from,unsigned long size)
{
	unsigned long *pg_table;
	unsigned long * dir, nr;

	if (from & 0x3fffff)/*只能处理4M边界处的线性基址。低 22=12+10 位必须为0，必须是一个页表项，对应一张页表 */
		panic("free_page_tables called with wrong alignment");
	if (!from)
		panic("Trying to free up swapper memory space");
	size = (size + 0x3fffff) >> 22;//处理成需要删除的页目录项个数
	dir = (unsigned long *) ((from>>20) & 0xffc); /*from为线性地址，页目录项起始地址（由于_pg_dir = 0，此处线性地址即物理地址） */
	for ( ; size-->0 ; dir++) {
          if (!(1 & *dir))/*只能释放页表存在于内存的对应页表（对应页目录项P=1）*/
			continue;
          pg_table = (unsigned long *) (0xfffff000 & *dir);/*取的页表首地址*/
		for (nr=0 ; nr<1024 ; nr++) {
			if (*pg_table) {
                          if (1 & *pg_table)/*在主内存区中*/
					free_page(0xfffff000 & *pg_table);
                          else/*在交换分区中*/
					swap_free(*pg_table >> 1);
				*pg_table = 0;
			}
			pg_table++;
		}
		free_page(0xfffff000 & *dir);/*释放页表*/
		*dir = 0;
	}
	invalidate();//通过重新加载cr3来刷新页目录缓存（缓存页目录到物理地址的映射关系）
	return 0;
}

/*
 *  Well, here is one of the most complicated functions in mm. It
 * copies a range of linerar addresses by copying only the pages.
 * Let's hope this is bug-free, 'cause this one I don't want to debug :-)
 *
 * Note! We don't copy just any chunks of memory - addresses have to
 * be divisible by 4Mb (one page-directory entry), as this makes the
 * function easier. It's used only by fork anyway.
 *
 * NOTE 2!! When from==0 we are copying kernel space for the first
 * fork(). Then we DONT want to copy a full page-directory entry, as
 * that would lead to some serious memory waste - we just copy the
 * first 160 pages - 640kB. Even that is more than we need, but it
 * doesn't take any more memory - we don't copy-on-write in the low
 * 1 Mb-range, so the pages can be shared with the kernel. Thus the
 * special case for nr=xxxx.
 */
int copy_page_tables(unsigned long from,unsigned long to,long size)
{
	unsigned long * from_page_table;
	unsigned long * to_page_table;
	unsigned long this_page;
	unsigned long * from_dir, * to_dir;
	unsigned long new_page;
	unsigned long nr;

	if ((from&0x3fffff) || (to&0x3fffff))
		panic("copy_page_tables called with wrong alignment");
	from_dir = (unsigned long *) ((from>>20) & 0xffc); /* _pg_dir = 0 */
	to_dir = (unsigned long *) ((to>>20) & 0xffc);
	size = ((unsigned) (size+0x3fffff)) >> 22;//向上取整，页目录项数
	for( ; size-->0 ; from_dir++,to_dir++) {
		if (1 & *to_dir)
			panic("copy_page_tables: already exist");
		if (!(1 & *from_dir))
			continue;
		from_page_table = (unsigned long *) (0xfffff000 & *from_dir);/*页目录项对应页表基址*/
		if (!(to_page_table = (unsigned long *) get_free_page()))//申请一张页表，线性地址
			return -1;	/* Out of memory, see freeing */
		*to_dir = ((unsigned long) to_page_table) | 7;//在页目录项中登记
		nr = (from==0)?0xA0:1024;//0xA0=10*16=160,160*4096=640KB
		for ( ; nr-- > 0 ; from_page_table++,to_page_table++) {
                  this_page = *from_page_table;//页表中的一项，对应一个内存页
			if (!this_page)
				continue;
			if (!(1 & this_page)) {//待复制内容在swap分区中
                          if (!(new_page = get_free_page()))//新申请一页物理内存
					return -1;
                          read_swap_page(this_page>>1, (char *) new_page);//从swap分区中复制内容
                          *to_page_table = this_page;
                          *from_page_table = new_page | (PAGE_DIRTY | 7);
                          continue;
			}
			this_page &= ~2;//共享内存页，采用写时复制技术
			*to_page_table = this_page;
			if (this_page > LOW_MEM) {
				*from_page_table = this_page;
				this_page -= LOW_MEM;
				this_page >>= 12;
				mem_map[this_page]++;//对应主内存页占用次数增1
			}
		}
	}
	invalidate();//mov 0,%eax; mov %eax, cr3
	return 0;
}

/*
 * This function puts a page in memory at the wanted address.
 * It returns the physical address of the page gotten, 0 if
 * out of memory (either when trying to access page-table or
 * page.)
 */
/*
  把物理地址page注册到线性地址address中
  page:物理地址
  address：线性地址

  本函数不足：page_table这个变量用于多种用途，建议一个变量仅有一种用途
 */
static unsigned long put_page(unsigned long page,unsigned long address)
{
	unsigned long tmp, *page_table;

/* NOTE !!! This uses the fact that _pg_dir=0 */

	if (page < LOW_MEM || page >= HIGH_MEMORY)//待分配的物理内存应该在主内存区
          printk("Trying to put page %p at %p\n",page,address);//应该为：panic
	if (mem_map[(page-LOW_MEM)>>12] != 1)//物理内存页应该是已经被分配
          printk("mem_map disagrees with %p at %p\n",page,address);//应该为:panic
	page_table = (unsigned long *) ((address>>20) & 0xffc);//页目录项地址
	if ((*page_table)&1)//对应页表在内存中
          page_table = (unsigned long *) (0xfffff000 & *page_table);//页表基址
	else {/*对应页表不在内存中*/
          if (!(tmp=get_free_page()))//申请一个内存页作为页表
			return 0;
		*page_table = tmp | 7;
		page_table = (unsigned long *) tmp;//页表指针
	}
        // (address>>12) & 0x3ff :  页表中页表项index
	page_table[(address>>12) & 0x3ff] = page | 7;//注册对应物理地址
/* no need for invalidate */
	return page;
}

/*
 * The previous function doesn't work very well if you also want to mark
 * the page dirty: exec.c wants this, as it has earlier changed the page,
 * and we want the dirty-status to be correct (for VM). Thus the same
 * routine, but this time we mark it dirty too.
 */
/*
基本同put_page,把线性地址address映射到page，同时置位页面dirty标志
 */
unsigned long put_dirty_page(unsigned long page, unsigned long address)
{
	unsigned long tmp, *page_table;

/* NOTE !!! This uses the fact that _pg_dir=0 */

	if (page < LOW_MEM || page >= HIGH_MEMORY)
		printk("Trying to put page %p at %p\n",page,address);
	if (mem_map[(page-LOW_MEM)>>12] != 1)
		printk("mem_map disagrees with %p at %p\n",page,address);
	page_table = (unsigned long *) ((address>>20) & 0xffc);//页目录项地址
	if ((*page_table)&1)
          page_table = (unsigned long *) (0xfffff000 & *page_table);//页表地址
	else {
		if (!(tmp=get_free_page()))
			return 0;
		*page_table = tmp|7;
		page_table = (unsigned long *) tmp;
	}
	page_table[(address>>12) & 0x3ff] = page | (PAGE_DIRTY | 7);
/* no need for invalidate */
	return page;
}

/*
取消页保护
写时复制的关键函数
 */
void un_wp_page(unsigned long * table_entry)
{
	unsigned long old_page,new_page;

	old_page = 0xfffff000 & *table_entry;//对应物理地址
        /*
          old_page >= LOW_MEM：表示在主内存区。
          mem_map[MAP_NR(old_page)]==1：表示对应页面引用次数为1，没有被共享
        */
	if (old_page >= LOW_MEM && mem_map[MAP_NR(old_page)]==1) {
          *table_entry |= 2;//置页面可写标志
          invalidate();//刷新页变换高速缓冲,通过重新加载页目录基址来实现(mov 0,eax;mov eax,cr3)。在 include/linux/mm.h中定义
		return;
	}
        
        /*以下处理页面被共享情况
          1.申请新内存页
          2.取消内存共享
          3.修改内存页属性为可写
         */
	if (!(new_page=get_free_page()))//申请新内存页
		oom();
	if (old_page >= LOW_MEM)
		mem_map[MAP_NR(old_page)]--;
	copy_page(old_page,new_page);//复制页面
	*table_entry = new_page | 7;
	invalidate();//刷新页变换高速缓冲
}	

/*
 * This routine handles present pages, when users try to write
 * to a shared page. It is done by copying the page to a new address
 * and decrementing the shared-page counter for the old page.
 *
 * If it's in code space we exit with a segment error.
 */
/*
写共享页面处理函数
address为线性地址
 */
void do_wp_page(unsigned long error_code,unsigned long address)
{
  //TASK_SIZE=64MB,进程内存空间大小
  if (address < TASK_SIZE)//指向内核地址
		printk("\n\rBAD! KERNEL MEMORY WP-ERR!\n\r");
  if (address - current->start_code > TASK_SIZE) {//不在进程的有效地址空间内（0-64MB）
		printk("Bad things happen: page error in do_wp_page\n\r");
		do_exit(SIGSEGV);
	}
#if 0
/* we cannot do this yet: the estdio library writes to code space */
/* stupid, stupid. I really want the libc.a from GNU */
  if (CODE_SPACE(address))//address指向代码段
		do_exit(SIGSEGV);
#endif
	un_wp_page((unsigned long *)
		(((address>>10) & 0xffc) + (0xfffff000 &
                                            *((unsigned long *) ((address>>20) &0xffc)))));//参数为页表项

}

/*
  验证address所在页面的可写性。如果不可读，则复制一页数据，改变其属性为可读(un_wp_page)。
  address:线性空间地址
*/
void write_verify(unsigned long address)
{
	unsigned long page;
        /*
0-11：页内偏移
12-21：页表index，对应一页内存
22-31：页目录项index，对应一张页表
0xfc=0b 1111 1100

页目录项index为：address>>22,
由于页目录基址为0，每个页目录项占4字节，所以索引为index的目录起始地址为：address>>22<<2
         */
	if (!( (page = *((unsigned long *) ((address>>20) & 0xffc)) )&1))//对应页表基址内容，及页表地址。对应页表不存在则退出
		return;
	page &= 0xfffff000;//页表基址
	page += ((address>>10) & 0xffc);//对应页表项地址
	if ((3 & *(unsigned long *) page) == 1)  /*0：P，1：R/W。non-writeable（0）, present（1） */
          un_wp_page((unsigned long *) page);//解除写保护.page：内存页指针
	return;
}

/*
为线性地址申请一页内存空间，同时在页目录页表中注册
 */
void get_empty_page(unsigned long address)
{
	unsigned long tmp;

	if (!(tmp=get_free_page()) || !put_page(tmp,address)) {
		free_page(tmp);		/* 0 is ok - ignored */
		oom();
	}
}

/*
 * try_to_share() checks the page at address "address" in the task "p",
 * to see if it exists, and if it is clean. If so, share it with the current
 * task.
 *
 * NOTE! This assumes we have checked that p != current, and that they
 * share the same executable or library.
 */
/*
将进程p的逻辑地址为address的页面共享给当前进程
address:逻辑地址
 */
static int try_to_share(unsigned long address, struct task_struct * p)
{
	unsigned long from;
	unsigned long to;
	unsigned long from_page;
	unsigned long to_page;
	unsigned long phys_addr;

	from_page = to_page = ((address>>20) & 0xffc);
	from_page += ((p->start_code>>20) & 0xffc);//线性地址
	to_page += ((current->start_code>>20) & 0xffc);//线性地址
        
/* is there a page-directory at from? */
	from = *(unsigned long *) from_page;//对应页表
	if (!(from & 1))
		return 0;
	from &= 0xfffff000;
	from_page = from + ((address>>10) & 0xffc);//页表项地址
	phys_addr = *(unsigned long *) from_page;//也表项内容，即物理地址
/* is the page clean and present? */
	if ((phys_addr & 0x41) != 0x01)
		return 0;
        //内存页存在且干净
	phys_addr &= 0xfffff000;
	if (phys_addr >= HIGH_MEMORY || phys_addr < LOW_MEM)
		return 0;

	to = *(unsigned long *) to_page;//页表项内容，即页表地址+属性
	if (!(to & 1))//页表不存在
          if (to = get_free_page())//页表地址
			*(unsigned long *) to_page = to | 7;
		else
			oom();
	to &= 0xfffff000;//页表地址
	to_page = to + ((address>>10) & 0xffc);//页表项地址
	if (1 & *(unsigned long *) to_page)
		panic("try_to_share: to_page already exists");
/* share them: write-protect */
	*(unsigned long *) from_page &= ~2;
	*(unsigned long *) to_page = *(unsigned long *) from_page;
	invalidate();
	phys_addr -= LOW_MEM;
	phys_addr >>= 12;
	mem_map[phys_addr]++;
	return 1;
}

/*
 * share_page() tries to find a process that could share a page with
 * the current one. Address is the address of the wanted page relative
 * to the current data space.
 *
 * We first check if it is at all feasible by checking executable->i_count.
 * It should be >1 if there are other tasks sharing this inode.
 */
/*
发生缺页中断时，首先看看能否与运行同一个可执行文件的其他进程进行共享页面共享处理
首先判断是否有进程运行同样的可执行文件
若有，则尝试寻找这样的进程，并尝试共享
判断系统是否有另一个进程也在执行同一个执行文件的方式是利用进程任务数据结构executable字段（或liabray字段）。该字段指向进程正在执行程序在内存中的inode，根据inode的引用次数i_count可以进行判断
 */
static int share_page(struct m_inode * inode, unsigned long address)
{
	struct task_struct ** p;

	if (inode->i_count < 2 || !inode)//应该为：(!inode || inode->i_count<2)
		return 0;
	for (p = &LAST_TASK ; p > &FIRST_TASK ; --p) {
		if (!*p)
			continue;
		if (current == *p)
			continue;
		if (address < LIBRARY_OFFSET) {
                  if (inode != (*p)->executable)//进程执行文件inode
				continue;
		} else {
                  if (inode != (*p)->library)//进程使用库文件inode
				continue;
		}
		if (try_to_share(address,*p))
			return 1;
	}
	return 0;
}

/*
缺页处理
address:线性地址，引起缺页中断的地址
 */
void do_no_page(unsigned long error_code,unsigned long address)
{
	int nr[4];
	unsigned long tmp;
	unsigned long page;
	int block,i;
	struct m_inode * inode;

        //先判线性地址是否满足条件
	if (address < TASK_SIZE)//是否在代码段
		printk("\n\rBAD!! KERNEL PAGE MISSING\n\r");
	if (address - current->start_code > TASK_SIZE) {//是否在有效的进程空间内
		printk("Bad things happen: nonexistent page error in do_no_page\n\r");
		do_exit(SIGSEGV);
	}
	page = *(unsigned long *) ((address >> 20) & 0xffc);//页目录项对应页表地址+属性
	if (page & 1) {//页表存在
		page &= 0xfffff000;
		page += (address >> 10) & 0xffc;//页表项地址
		tmp = *(unsigned long *) page;//内存页地址+属性
		if (tmp && !(1 & tmp)) {//内存页存在，但在交换分区中
			swap_in((unsigned long *) page);页表项地址
			return;
		}
	}
        
        //对应页表不存在
	address &= 0xfffff000;//仅保留页目录项index和页表项index
	tmp = address - current->start_code;//偏移，4096的倍数，也就是逻辑地址
	if (tmp >= LIBRARY_OFFSET ) {//库文件
		inode = current->library;
		block = 1 + (tmp-LIBRARY_OFFSET) / BLOCK_SIZE;
	} else if (tmp < current->end_data) {//可执行文件
		inode = current->executable;
		block = 1 + tmp / BLOCK_SIZE;
	} else {//进程访问其动态申请的页面或者存放栈信息而引起的缺页中断
		inode = NULL;
		block = 0;
	}
	if (!inode) {//进程访问其动态申请的页面或者存放栈信息而引起的缺页中断，直接申请一页内存就ok
		get_empty_page(address);
		return;
	}
	if (share_page(inode,tmp))
		return;
        
	if (!(page = get_free_page()))//内存页
		oom();
/* remember that 1 block is used for header */
	for (i=0 ; i<4 ; block++,i++)
		nr[i] = bmap(inode,block);
	bread_page(page,inode->i_dev,nr);//从高速缓冲中读取进内存页（主内存区）
        //如果current->end_data不在内存页边界处，则需要把对应页后部分清0。如果tmp不在执行文件中读取的文件，则是在库文件中读取的，不用执行清零操作
        //注意如果tmp和end_data在同一内存页，才需要做后面清零操作
	i = tmp + 4096 - current->end_data;//需要清零的长度
	if (i>4095)/*应该为: (i>4095 || i<0)*/
		i = 0;
	tmp = page + 4096;
	while (i-- > 0) {
		tmp--;
		*(char *)tmp = 0;
	}
        //把引起缺页中断的物理页面映射到指定线性地址，否则释放内存页，显示内存不够
	if (put_page(page,address))//把线性地址address映射到物理地址page
		return;
	free_page(page);
	oom();
}

void mem_init(long start_mem, long end_mem)
{
	int i;

	HIGH_MEMORY = end_mem;
	for (i=0 ; i<PAGING_PAGES ; i++)
          mem_map[i] = USED;//USED=100
	i = MAP_NR(start_mem);//主内存区其实编号
	end_mem -= start_mem;
	end_mem >>= 12;//内存页数
	while (end_mem-->0)
          mem_map[i++]=0;//主内存区清零，即标记为未用状态
}

/*
free的作用？？？
 */
void show_mem(void)
{
	int i,j,k,free=0,total=0;
	int shared=0;
	unsigned long * pg_tbl;

	printk("Mem-info:\n\r");
	for(i=0 ; i<PAGING_PAGES ; i++) {
		if (mem_map[i] == USED)
			continue;
		total++;//主内存页面数
		if (!mem_map[i])
                  free++;//空闲主内存页面数
		else
                  shared += mem_map[i]-1;//被共享内存次数
	}
	printk("%d free pages of %d\n\r",free,total);
	printk("%d pages shared\n\r",shared);
	k = 0;//记录进程所占内存页数
	for(i=4 ; i<1024 ;) {//页目录项编号，内核占4页面，不列入统计范畴
          if (1&pg_dir[i]) {//页目录对应页表存在
            if (pg_dir[i]>HIGH_MEMORY) {//页目录项有问题
				printk("page directory[%d]: %08X\n\r",
					i,pg_dir[i]);
				continue;
			}
			if (pg_dir[i]>LOW_MEM)
				free++,k++;
			pg_tbl=(unsigned long *) (0xfffff000 & pg_dir[i]);//取对应页表基址
			for(j=0 ; j<1024 ; j++)//页表index
				if ((pg_tbl[j]&1) && pg_tbl[j]>LOW_MEM)
                                  if (pg_tbl[j]>HIGH_MEMORY)//有问题
						printk("page_dir[%d][%d]: %08X\n\r",
							i,j, pg_tbl[j]);
					else
						k++,free++;
		}
          i++;
          //每个任务64MB，即一个进程占用最多16个目录项
          //页目录常驻内存开始处0-4096共1024项。1024/16=64个进程
          if (!(i&15) && k) {//一个进程的统计完成
			k++,free++;	/* one page/process for task_struct */
			printk("Process %d: %d pages\n\r",(i>>4)-1,k);//
			k = 0;
          }
	}
	printk("Memory found: %d (%d)\n\r",free-shared,total);
}
