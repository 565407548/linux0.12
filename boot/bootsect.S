!
! SYS_SIZE is the number of clicks (16 bytes) to be loaded.
! 0x3000 is 0x30000 bytes = 196kB, more than enough for current
! versions of linux
!
!文件扩展名为.S,表示先利用CPP进行预处理
#include <linux/config.h>
!DEF_SYSSIZE 在linux/config.h中定义
SYSSIZE = DEF_SYSSIZE
!
!	bootsect.s		(C) 1991 Linus Torvalds
!	modified by Drew Eckhardt
!
! bootsect.s is loaded at 0x7c00 by the bios-startup routines, and moves
! iself out of the way to address 0x90000, and jumps there.
! bootsect.S 大小为512字节，即0x90000-0x90200
!	
! It then loads 'setup' directly after itself (0x90200), and the system
! at 0x10000, using BIOS interrupts. 
!
! NOTE! currently system is at most 8*65536 bytes long. This should be no
! problem, even in the future. I want to keep it simple. This 512 kB
! kernel size should be enough, especially as this doesn't contain the
! buffer cache as in minix
!
! The loader has been made as simple as possible, and continuos
! read errors will result in a unbreakable loop. Reboot by hand. It
! loads pretty fast by getting whole sectors at a time whenever possible.

!代码数据同段	
.globl begtext, begdata, begbss, endtext, enddata, endbss
.text
begtext:
.data
begdata:
.bss
begbss:
.text

SETUPLEN = 4				! nr of setup-sectors
BOOTSEG  = 0x07c0			! original address of boot-sector
INITSEG  = DEF_INITSEG			! we move boot here - out of the way
SETUPSEG = DEF_SETUPSEG			! setup starts here
SYSSEG   = DEF_SYSSEG			! system loaded at 0x10000 (65536).
ENDSEG   = SYSSEG + SYSSIZE		! where to stop loading

! ROOT_DEV & SWAP_DEV are now written by "build".
ROOT_DEV = 0
SWAP_DEV = 0

entry start
start:
	mov	ax,#BOOTSEG
	mov	ds,ax
	mov	ax,#INITSEG
	mov	es,ax
	mov	cx,#256
	sub	si,si
	sub	di,di

	rep
	movw
!movw:[ds:si]->[es:di],每次移动2字节，移动512共需要256次
	jmpi	go,INITSEG

go:	mov	ax,cs		! 0x9000
	mov	dx,#0xfef4	! arbitrary value >>512 - disk parm size

	mov	ds,ax
	mov	es,ax
	push	ax		!此句有问题,ax=cs=0x9000

	mov	ss,ax		! put stack at 0x9ff00 - 12.
	mov	sp,dx		! fef4+12=ff00
/*
 *	Many BIOS's default disk parameter tables will not 
 *	recognize multi-sector reads beyond the maximum sector number
 *	specified in the default diskette parameter tables - this may
 *	mean 7 sectors in some cases.
 *
 *	Since single sector reads are slow and out of the question,
 *	we must take care of this by creating new parameter tables
 *	(for the first disk) in RAM.  We will set the maximum sector
 *	count to 18 - the most we will encounter on an HD 1.44.  
 *
 *	High doesn't hurt.  Low does.
 *	一次可读取的扇区数越大越好，大于18都不会有问题，否则由于后面一次需要读取18，设置太小会出问题
 *
 *	Segments are as follows: ds=es=ss=cs - INITSEG(0x9000),
 *		fs = 0, gs = parameter table segment
 */

!0x78出存放的是软驱参数表地址
	push	#0
	pop	fs
	mov	bx,#0x78		! fs:bx is parameter table address
	seg fs
	lgs	si,(bx)			! 取地址，[fs:bx]->[gs:si] is source

	mov	di,dx			! dx=0xfef4,es:di is destination
	mov	cx,#6			! copy 12 bytes
	cld

	rep
	seg gs
	movw				![es:di]<-[gs:si]

	mov	di,dx
	movb	4(di),*18		! patch sector count,修改一次可读取的最大扇区数为18

	seg fs				!fs=0,bx=0x78,让中断向量0x1E指向新表（即新的软驱参数表）
	mov	(bx),di			!0xfef4
	seg fs	
	mov	2(bx),es		!0x9000

	pop	ax			!ax=0x9000
	mov	fs,ax			
	mov	gs,ax
	
	xor	ah,ah			! reset FDC 
	xor	dl,dl
	int 	0x13	

! load the setup-sectors directly after the bootblock.
! Note that 'es' is already set up.

load_setup:
	xor	dx, dx			! drive 0, head 0
	mov	cx,#0x0002		! sector 2, track 0
	mov	bx,#0x0200		! address = 512, in INITSEG(0x9000)
	mov	ax,#0x0200+SETUPLEN	! service 2, nr of sectors,SETUPLEN=4,即4个扇区。一次读取完，上面已经设置了一次最多18个扇区，此处完全可以一次读取4个扇区
	int	0x13			! read it
	jnc	ok_load_setup		! ok - continue

	push	ax			! dump error code
	call	print_nl
	mov	bp, sp
	call	print_hex
	pop	ax	
	
	xor	dl, dl			! reset FDC
	xor	ah, ah
	int	0x13
	j	load_setup

ok_load_setup:

! Get disk drive parameters, specifically nr of sectors/track
!读取磁盘参数，此处主要是获取每磁道扇区数，并存与sectors
!0x13 读取软盘参数表相关信息
	xor	dl,dl
	mov	ah,#0x08		! AH=8 is get drive parameters
	int	0x13
	xor	ch,ch
	seg cs				!0x9000
	mov	sectors,cx
	
	mov	ax,#INITSEG		!0x9000
	mov	es,ax

! Print some inane message
!0x10 ah=0x03 读取当前光标位置
	mov	ah,#0x03		! read cursor pos
	xor	bh,bh
	int	0x10			!dx保存当前光标位置
!0x10 ah=0x13 显示字符串
	mov	cx,#9			!显示字符数
	mov	bx,#0x0007		! page 0, attribute 7 (normal)
	mov	bp,#msg1		!es:bp字符串首地址
	mov	ax,#0x1301		! write string, move cursor
	int	0x10

! ok, we've written the message, now
! we want to load the system (at 0x10000)

	mov	ax,#SYSSEG
	mov	es,ax		! segment of 0x010000
	call	read_it
	call	kill_motor
	call	print_nl

! After that we check which root-device to use. If the device is
! defined (!= 0), nothing is done and the given device is used.
! Otherwise, either /dev/PS0 (2,28) or /dev/at0 (2,8), depending
! on the number of sectors that the BIOS reports currently.
! linux中软盘主设备号为2，次设备号为type*4+nr,nr=0-3(对应A，B，C，D)
	seg cs				!0x9000
	mov	ax,root_dev
	or	ax,ax
	jne	root_defined
	seg cs				!0x9000
	mov	bx,sectors		!每磁道扇区数
	mov	ax,#0x0208		! /dev/ps0 - 1.2Mb
	cmp	bx,#15
	je	root_defined
	mov	ax,#0x021c		! /dev/PS0 - 1.44Mb
	cmp	bx,#18
	je	root_defined
undef_root:
	jmp undef_root
root_defined:
	seg cs				!0x9000
	mov	root_dev,ax

! after that (everyting loaded), we jump to
! the setup-routine loaded directly after
! the bootblock:

	jmpi	0,SETUPSEG

! This routine loads the system at address 0x10000, making sure
! no 64kB boundaries are crossed. We try to load it as fast as
! possible, loading whole tracks whenever we can.
!
! in:	es - starting address segment (normally 0x1000)
!
sread:	.word 1+SETUPLEN	! sectors read of current track,SETUPLEN=4，已经读取到当前扇区的第1+SETUPLEN=5磁道
head:	.word 0			! current head
track:	.word 0			! current track

!在调用read_it前需给es赋值。本程序赋值为0x1000
read_it:
	mov ax,es
	test ax,#0x0fff
die:	jne die			! es must be at 64kB boundary
	xor bx,bx		! bx is starting address within segment
rp_read:
	mov ax,es
	cmp ax,#ENDSEG		! have we loaded all yet?
	jb ok1_read
	ret
ok1_read:
	seg cs			!cs=0x9000
	mov ax,sectors		!每磁道扇区数，
	sub ax,sread		!
	mov cx,ax		!本次读取扇区数
	shl cx,#9		!cx*512+段内当前偏移
	add cx,bx
	jnc ok2_read
	je ok2_read

!计算当前最多还可以读入的数据量，2^16=64KB
	xor ax,ax		!ax=0
	sub ax,bx		!ax=ax-bx=0-bx即为还可以读取的数据量
	shr ax,#9		!ax/2^9，获得扇区数
ok2_read:
	call read_track
	mov cx,ax
	add ax,sread
	seg cs
	cmp ax,sectors		!当前磁道是否还有未读完的扇区
	jne ok3_read
	mov ax,#1		!ax=1
	sub ax,head		!ax=ax-head
	jne ok4_read		
	inc track		!ax=0,head=1,track++
ok4_read:
	mov head,ax		!head=ax
	xor ax,ax
ok3_read:
	mov sread,ax
	shl cx,#9
	add bx,cx
	jnc rp_read
	mov ax,es		!es=0x9000
	add ah,#0x10
	mov es,ax		!
	xor bx,bx
	jmp rp_read

read_track:
	pusha
	pusha	
	mov	ax, #0xe2e 	! loading... message 2e = .
	mov	bx, #7
 	int	0x10
	popa		

	mov dx,track
	mov cx,sread
	inc cx
	mov ch,dl
	mov dx,head
	mov dh,dl
	and dx,#0x0100
	mov ah,#2
	
	push	dx				! save for error dump
	push	cx
	push	bx
	push	ax

	int 0x13
	jc bad_rt				!出错处理
	add	sp, #8   			!弹出保存的ax,bx,cx,dx
	popa
	ret

bad_rt:	push	ax				! save error code
	call	print_all			! ah = error, al = read
	
	
	xor ah,ah				!复位软盘控制器
	xor dl,dl
	int 0x13
	

	add	sp, #10				!10字节，调用bad_rt前压入的ax,bc,cx,dx,及本函数自己压入的ax,共5*2字节
	popa	
	jmp read_track

/*
 *	print_all is for debugging purposes.  
 *	It will print out all of the registers.  The assumption is that this is
 *	called from a routine, with a stack frame like
 *	dx 
 *	cx
 *	bx
 *	ax
 *	error(ax)
 *	ret <- sp(返回指针)
 *
*/
!输出依次输出：error_code,ax,bx,cx,dx
print_all:
	mov	cx, #5		! error code + 4 registers
	mov	bp, sp	

print_loop:
	push	cx		! save count left
	call	print_nl	! nl for readability
	jae	no_reg		! see if register name is needed
				!CF=0,则不显示寄存器名称。
	mov	ax, #0xe05 + 0x41 - 1
	sub	al, cl
	int	0x10

	mov	al, #0x58 	! X
	int	0x10

	mov	al, #0x3a 	! :
	int	0x10

no_reg:
	add	bp, #2		! next register
	call	print_hex	! print it
	pop	cx
	loop	print_loop
	ret

print_nl:
	mov	ax, #0xe0d	! CR
	int	0x10
	mov	al, #0xa	! LF
	int 	0x10
	ret

/*
 *	print_hex is for debugging purposes, and prints the word
 *	pointed to by ss:bp in hexadecmial.
*/

print_hex:
	mov	cx, #4		! 4 hex digits
	mov	dx, (bp)	! load word into dx
print_digit:
	rol	dx, #4		! rotate from right to left. rotate so that lowest 4 bits are used
	mov	ah, #0xe	
	mov	al, dl		! mask off so we have only next nibble
	and	al, #0xf
	add	al, #0x30	! convert to 0 based digit, '0'
	cmp	al, #0x39	! check for overflow
	jbe	good_digit
	add	al, #0x41 - 0x30 - 0xa 	! 'A' - '0' - 0xa(10)

good_digit:
	int	0x10
	loop	print_digit
	ret


/*
 * This procedure turns off the floppy drive motor, so
 * that we enter the kernel in a known state, and
 * don't have to worry about it later.
 */
kill_motor:
	push dx
	mov dx,#0x3f2
	xor al, al
	outb			!ax->port dx
	pop dx
	ret

sectors:
	.word 0

msg1:
	.byte 13,10
	.ascii "Loading"

.org 506
swap_dev:
	.word SWAP_DEV
root_dev:
	.word ROOT_DEV
boot_flag:
	.word 0xAA55

.text
endtext:
.data
enddata:
.bss
endbss:

