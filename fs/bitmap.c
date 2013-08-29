/*
 *  linux/fs/bitmap.c
 *
 *  (C) 1991  Linus Torvalds
 */

/* bitmap.c contains the code that handles the inode and block bitmaps */
#include <string.h>

#include <linux/sched.h>
#include <linux/kernel.h>

/*
cld:clear direction
stosl: eax -> [edi]
清零一个 BLOCK_SIZE 数据
*/
#define clear_block(addr) \
__asm__("cld\n\t" \
	"rep\n\t" \
	"stosl" \
	::"a" (0),"c" (BLOCK_SIZE/4),"D" ((long) (addr)):"cx","di")

/*
btsl: bit test and set,把基地址和偏移处的值先保存进CF，然后再置位
btrl: bit test and reset，把基地址和偏移处的值先保存进CF，然后再复位
set_bit,clear_bit返回零表示一切正常。
%3(addr)：基址
%2(nr)：偏移
*/
#define set_bit(nr,addr) ({\
register int res __asm__("ax"); \
__asm__ __volatile__("btsl %2,%3\n\tsetb %%al": \
"=a" (res):"0" (0),"r" (nr),"m" (*(addr))); \
res;})

#define clear_bit(nr,addr) ({\
register int res __asm__("ax"); \
__asm__ __volatile__("btrl %2,%3\n\tsetnb %%al": \
"=a" (res):"0" (0),"r" (nr),"m" (*(addr))); \
res;})

/*
从基址（addr，1024的倍数）开始出，寻找第一个数据不为0的地址偏移
一块大小为1024，所以有 8192 bit
lodsl: [esi] -> eax
bsfl:从位0开始扫描为1的位，偏移入edx。扫描数据全0时，设置ZF=1
je: jump if ZF is set
ecx记录偏移值
*/
#define find_first_zero(addr) ({ \
int __res; \
__asm__("cld\n" \
	"1:\tlodsl\n\t" \
	"notl %%eax\n\t" \
	"bsfl %%eax,%%edx\n\t" \
	"je 2f\n\t" \
	"addl %%edx,%%ecx\n\t" \
	"jmp 3f\n" \
	"2:\taddl $32,%%ecx\n\t" \
	"cmpl $8192,%%ecx\n\t" \
	"jl 1b\n" \
	"3:" \
	:"=c" (__res):"c" (0),"S" (addr):"ax","dx","si"); \
__res;})

/*
1.释放高速缓存中对应缓存
2.复位数据位图
3.标记数据位图已经更改（b_dirt=1）
*/
int free_block(int dev, int block)
{
  struct super_block * sb;
  struct buffer_head * bh;

  if (!(sb = get_super(dev)))
    panic("trying to free block on nonexistent device");
  if (block < sb->s_firstdatazone || block >= sb->s_nzones)//硬盘数据区
    panic("trying to free block not in datazone");
  bh = get_hash_table(dev,block);//高速缓存
  if (bh) {
    if (bh->b_count > 1) {
      brelse(bh);
      return 0;
    }
    bh->b_dirt=0;
    bh->b_uptodate=0;
    if (bh->b_count)
      brelse(bh);
  }
  block -= sb->s_firstdatazone - 1 ;
  if (clear_bit(block&8191,sb->s_zmap[block/8192]->b_data)) {
    printk("block (%04x:%d) ",dev,block+sb->s_firstdatazone-1);
    printk("free_block: bit already cleared\n");
  }
  sb->s_zmap[block/8192]->b_dirt = 1;
  return 1;
}

/*
申请新磁盘块，同时申请与其对应的高速缓冲块.
通过getblk（buffer.c）建立新磁盘块与对应高速缓冲块之间的联系
 */
int new_block(int dev)
{
	struct buffer_head * bh;
	struct super_block * sb;
	int i,j;

	if (!(sb = get_super(dev)))
		panic("trying to get new block from nonexistant device");

        //查找 8 块数据位图数据
        j = 8192;
	for (i=0 ; i<8 ; i++)
		if (bh=sb->s_zmap[i])
			if ((j=find_first_zero(bh->b_data))<8192)
				break;
	if (i>=8 || !bh || j>=8192)
		return 0;
	if (set_bit(j,bh->b_data))
		panic("new_block: bit already set");
	bh->b_dirt = 1;
	j += i*8192 + sb->s_firstdatazone-1;//计算获得的数据块在整个磁盘的偏移
	if (j >= sb->s_nzones)
		return 0;
	if (!(bh=getblk(dev,j)))
		panic("new_block: cannot get block");
	if (bh->b_count != 1)
		panic("new block: count is != 1");
	clear_block(bh->b_data);
	bh->b_uptodate = 1;
	bh->b_dirt = 1;
	brelse(bh);
	return j;
}

/*

 */
void free_inode(struct m_inode * inode)
{
	struct super_block * sb;
	struct buffer_head * bh;

	if (!inode)
		return;
	if (!inode->i_dev) {
		memset(inode,0,sizeof(*inode));
		return;
	}
	if (inode->i_count>1) {
		printk("trying to free inode with count=%d\n",inode->i_count);
		panic("free_inode");
	}
	if (inode->i_nlinks)
		panic("trying to free inode with links");
	if (!(sb = get_super(inode->i_dev)))
		panic("trying to free inode on nonexistent device");
	if (inode->i_num < 1 || inode->i_num > sb->s_ninodes)
		panic("trying to free inode 0 or nonexistant inode");
	if (!(bh=sb->s_imap[inode->i_num>>13]))//2^13=8192
		panic("nonexistent imap in superblock");
	if (clear_bit(inode->i_num&8191,bh->b_data))//清除（基址，偏移）处对应位，返回该位之前数据的反（0 返回 1， 1 返回 0）
		printk("free_inode: bit already cleared.\n\r");
	bh->b_dirt = 1;
	memset(inode,0,sizeof(*inode));
}

/*
申请inode
1.获得全局inode结构体
2.获得对应磁盘中的一个空闲inode块偏移（由于是新申请的，不需要从磁盘中读）
3.建立二者的对应关系
 */
struct m_inode * new_inode(int dev)
{
	struct m_inode * inode;
	struct super_block * sb;
	struct buffer_head * bh;
	int i,j;

        //inode结构体数据为系统全局变量，通过get_empty_inode（inode.c）统一申请
	if (!(inode=get_empty_inode()))
		return NULL;
	if (!(sb = get_super(dev)))
		panic("new_inode with unknown device");
        // 8 块 inode 位图
	j = 8192;
	for (i=0 ; i<8 ; i++)
		if (bh=sb->s_imap[i])
			if ((j=find_first_zero(bh->b_data))<8192)
				break;
	if (!bh || j >= 8192 || j+i*8192 > sb->s_ninodes) {
          iput(inode);//释放inode
		return NULL;
	}
	if (set_bit(j,bh->b_data))//设置inode位图
		panic("new_inode: bit already set");
	bh->b_dirt = 1;
	inode->i_count=1;
	inode->i_nlinks=1;
	inode->i_dev=dev;
	inode->i_uid=current->euid;
	inode->i_gid=current->egid;
	inode->i_dirt=1;
	inode->i_num = j + i*8192;
	inode->i_mtime = inode->i_atime = inode->i_ctime = CURRENT_TIME;
	return inode;
}
