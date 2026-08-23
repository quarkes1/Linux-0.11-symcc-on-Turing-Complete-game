/*
 *  linux/kernel/console.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 *	console.c
 *
 * This module implements the console io functions
 *	'void con_init(void)'
 *	'void con_write(struct tty_queue * queue)'
 * Hopefully this will be a rather complete VT102 implementation.
 *
 * Beeping thanks to John T Kohl.
 */

/*
 *  NOTE!!! We sometimes disable and enable interrupts for a short while
 * (to put a word in video IO), but this will work even for keyboard
 * interrupts. We know interrupts aren't enabled when getting a keyboard
 * interrupt, as we use trap-gates. Hopefully all is well.
 */

/*
 * Code to check for different video-cards mostly by Galen Hunt,
 * <g-hunt@ee.utah.edu>
 */

#include <linux/sched.h>
#include <linux/tty.h>
#include <asm/io.h>
#include <asm/system.h>

/*
 * These are set up by the setup-routine at boot-time:
 */

#define ORIG_X			(*(unsigned char *)0x90000)
#define ORIG_Y			(*(unsigned char *)0x90001)
#define ORIG_VIDEO_PAGE		(*(unsigned short *)0x90004)
#define ORIG_VIDEO_MODE		((*(unsigned short *)0x90006) & 0xff)
#define ORIG_VIDEO_COLS 	(((*(unsigned short *)0x90006) & 0xff00) >> 8)
#define ORIG_VIDEO_LINES	(25)
#define ORIG_VIDEO_EGA_AX	(*(unsigned short *)0x90008)
#define ORIG_VIDEO_EGA_BX	(*(unsigned short *)0x9000a)
#define ORIG_VIDEO_EGA_CX	(*(unsigned short *)0x9000c)

#define VIDEO_TYPE_MDA		0x10	/* Monochrome Text Display	*/
#define VIDEO_TYPE_CGA		0x11	/* CGA Display 			*/
#define VIDEO_TYPE_EGAM		0x20	/* EGA/VGA in Monochrome Mode	*/
#define VIDEO_TYPE_EGAC		0x21	/* EGA/VGA in Color Mode	*/

#define NPAR 16

extern void keyboard_interrupt(void);

static unsigned char	video_type;		/* Type of display being used	*/
static unsigned long	video_num_columns;	/* Number of text columns	*/
static unsigned long	video_size_row;		/* Bytes per row		*/
static unsigned long	video_num_lines;	/* Number of test lines		*/
static unsigned char	video_page;		/* Initial video page		*/
static unsigned long	video_mem_start;	/* Start of video RAM		*/
static unsigned long	video_mem_end;		/* End of video RAM (sort of)	*/
static unsigned short	video_port_reg;		/* Video register select port	*/
static unsigned short	video_port_val;		/* Video register value port	*/
static unsigned short	video_erase_char;	/* Char+Attrib to erase with	*/

static unsigned long	origin;		/* Used for EGA/VGA fast scroll	*/
static unsigned long	scr_end;	/* Used for EGA/VGA fast scroll	*/
static unsigned long	pos;
static unsigned long	x,y;
static unsigned long	top,bottom;
static unsigned long	state=0;
static unsigned long	npar,par[NPAR];
static unsigned long	ques=0;
static unsigned char	attr=0x07;

static void sysbeep(void);

/*
 * this is what the terminal answers to a ESC-Z or csi0c
 * query (= vt100 response).
 */
#define RESPONSE "\033[?1;2c"

/* NOTE! gotoxy thinks x==video_num_columns is ok */
static inline void gotoxy(unsigned int new_x,unsigned int new_y)
{
	/* SYMPLUS-PORT: 拆分 `||` 为两个独立 if——symcc 对
	 * `if (a || b) return;` 的跳转目标有误（STUB 落入 then 块导致恒 return） */
	if (new_x > video_num_columns)
		return;
	if (new_y >= video_num_lines)
		return;
	x=new_x;
	y=new_y;
	/* SYMPLUS-PORT: M3 字节单元（游戏屏每字符 1 字节），不再 x<<1 */
	pos=origin + y*video_size_row + x;
}

static inline void set_origin(void)
{
	cli();
	outb_p(12, video_port_reg);
	outb_p(0xff&((origin-video_mem_start)>>9), video_port_val);
	outb_p(13, video_port_reg);
	outb_p(0xff&((origin-video_mem_start)>>1), video_port_val);
	sti();
}

static void scrup(void)
{
	if (video_type == VIDEO_TYPE_EGAC || video_type == VIDEO_TYPE_EGAM)
	{
		if (!top && bottom == video_num_lines) {
			origin += video_size_row;
			pos += video_size_row;
			scr_end += video_size_row;
			if (scr_end > video_mem_end) {
/* SYMPLUS-PORT: rep movsl+stosw scroll -> C loop */
				{
					unsigned short *_d = (unsigned short *)video_mem_start;
					unsigned short *_s = (unsigned short *)origin;
					unsigned short *_e = (unsigned short *)(scr_end - video_size_row);
					int _i;
					for (_i = 0; _i < (video_num_lines-1)*video_num_columns; _i++)
						_d[_i] = _s[_i];
					for (_i = 0; _i < video_num_columns; _i++)
						_e[_i] = video_erase_char;
				}
				scr_end -= origin-video_mem_start;
				pos -= origin-video_mem_start;
				origin = video_mem_start;
			} else {
/* SYMPLUS-PORT: rep stosw erase -> C loop */
				{
					unsigned short *_e = (unsigned short *)(scr_end - video_size_row);
					int _i;
					for (_i = 0; _i < video_num_columns; _i++)
						_e[_i] = video_erase_char;
				}
			}
			set_origin();
		} else {
/* SYMPLUS-PORT: rep movsl+stosw scroll -> C loop */
		{
			unsigned short *_d = (unsigned short *)(origin + video_size_row*top);
			unsigned short *_s = (unsigned short *)(origin + video_size_row*(top+1));
			unsigned short *_e = (unsigned short *)(origin + video_size_row*(bottom-1));
			int _i;
			for (_i = 0; _i < (bottom-top-1)*video_num_columns; _i++)
				_d[_i] = _s[_i];
			for (_i = 0; _i < video_num_columns; _i++)
				_e[_i] = video_erase_char;
		}
		}
	}
	else		/* Not EGA/VGA */
	{
/* SYMPLUS-PORT: rep movsl+stosw scroll -> C loop（M3 字节单元，游戏屏）
 * video_type 硬编码 CGA，EGA 快速滚动分支为死代码，未改写 */
		{
			unsigned char *_d = (unsigned char *)(origin + video_size_row*top);
			unsigned char *_s = (unsigned char *)(origin + video_size_row*(top+1));
			unsigned char *_e = (unsigned char *)(origin + video_size_row*(bottom-1));
			int _i;
			for (_i = 0; _i < (bottom-top-1)*video_num_columns; _i++)
				_d[_i] = _s[_i];
			for (_i = 0; _i < video_num_columns; _i++)
				_e[_i] = video_erase_char;
		}
	}
}

static void scrdown(void)
{
	if (video_type == VIDEO_TYPE_EGAC || video_type == VIDEO_TYPE_EGAM)
	{
/* SYMPLUS-PORT: std rep movsl+stosw scroll-down -> C reverse loop */
		{
			unsigned short *_d = (unsigned short *)(origin + video_size_row*(top+1));
			unsigned short *_s = (unsigned short *)(origin + video_size_row*top);
			unsigned short *_e = (unsigned short *)origin;
			int _i;
			for (_i = (bottom-top-1)*video_num_columns - 1; _i >= 0; _i--)
				_d[_i] = _s[_i];
			for (_i = 0; _i < video_num_columns; _i++)
				_e[_i] = video_erase_char;
		}
	}
	else		/* Not EGA/VGA */
	{
/* SYMPLUS-PORT: std rep movsl+stosw scroll-down -> C reverse loop（M3 字节单元） */
		{
			unsigned char *_d = (unsigned char *)(origin + video_size_row*(top+1));
			unsigned char *_s = (unsigned char *)(origin + video_size_row*top);
			unsigned char *_e = (unsigned char *)origin;
			int _i;
			for (_i = (bottom-top-1)*video_num_columns - 1; _i >= 0; _i--)
				_d[_i] = _s[_i];
			for (_i = 0; _i < video_num_columns; _i++)
				_e[_i] = video_erase_char;
		}
	}
}

static void lf(void)
{
	/* SYMPLUS-PORT: M3 字节单元 —— 换行必须回列首。真实 0.11 依赖
	 * printk 字符串以 "\n\r" 结尾（\r 触发 cr() 回列首）；M3 启动链
	 * 字符串仅以 \n 结尾，若 lf() 只 pos += size_row，每行会落在
	 * "上一行末列 + 96" 处。这里先归零列（含 x，避免 x 跨行累积
	 * 触发假换行）。换行回绕路径调用 lf() 时 x 已为 0，pos -= x 无副作用。 */
	pos -= x;
	x = 0;
	if (y+1<bottom) {
		y++;
		pos += video_size_row;
		return;
	}
	scrup();
}

static void ri(void)
{
	if (y>top) {
		y--;
		pos -= video_size_row;
		return;
	}
	scrdown();
}

static void cr(void)
{
	/* SYMPLUS-PORT: M3 字节单元 */
	pos -= x;
	x=0;
}

static void del(void)
{
	if (x) {
		pos -= 1;	/* SYMPLUS-PORT: M3 字节单元 */
		x--;
		*(unsigned char *)pos = video_erase_char;
	}
}

static void csi_J(int par)
{
	long count;
	long start;

	switch (par) {
		case 0:	/* erase from cursor to end of display */
			count = scr_end-pos;	/* SYMPLUS-PORT: M3 字节单元（原 >>1） */
			start = pos;
			break;
		case 1:	/* erase from start to cursor */
			count = pos-origin;	/* SYMPLUS-PORT: M3 字节单元 */
			start = origin;
			break;
		case 2: /* erase whole display */
			count = video_num_columns * video_num_lines;
			start = origin;
			break;
		default:
			return;
	}
/* SYMPLUS-PORT: rep stosw erase -> C loop（M3 字节单元） */
	{
		unsigned char *_p = (unsigned char *)start;
		long _i;
		for (_i = 0; _i < count; _i++)
			_p[_i] = video_erase_char;
	}
}

static void csi_K(int par)
{
	long count;
	long start;

	switch (par) {
		case 0:	/* erase from cursor to end of line */
			if (x>=video_num_columns)
				return;
			count = video_num_columns-x;
			start = pos;
			break;
		case 1:	/* erase from start of line to cursor */
			start = pos - x;	/* SYMPLUS-PORT: M3 字节单元 */
			count = (x<video_num_columns)?x:video_num_columns;
			break;
		case 2: /* erase whole line */
			start = pos - x;	/* SYMPLUS-PORT: M3 字节单元 */
			count = video_num_columns;
			break;
		default:
			return;
	}
/* SYMPLUS-PORT: rep stosw erase -> C loop（M3 字节单元） */
	{
		unsigned char *_p = (unsigned char *)start;
		long _i;
		for (_i = 0; _i < count; _i++)
			_p[_i] = video_erase_char;
	}
}

void csi_m(void)
{
	int i;

	for (i=0;i<=npar;i++)
		switch (par[i]) {
			case 0:attr=0x07;break;
			case 1:attr=0x0f;break;
			case 4:attr=0x0f;break;
			case 7:attr=0x70;break;
			case 27:attr=0x07;break;
		}
}

static inline void set_cursor(void)
{
	cli();
	outb_p(14, video_port_reg);
	outb_p(0xff&((pos-video_mem_start)>>9), video_port_val);
	outb_p(15, video_port_reg);
	outb_p(0xff&((pos-video_mem_start)>>1), video_port_val);
	sti();
}

static void respond(struct tty_struct * tty)
{
	char * p = RESPONSE;

	cli();
	while (*p) {
		putch(*p,&tty->read_q);
		p++;
	}
	sti();
	copy_to_cooked(tty);
}

static void insert_char(void)
{
	int i=x;
	unsigned char tmp, old = video_erase_char;
	unsigned char * p = (unsigned char *) pos;

	while (i++<video_num_columns) {
		tmp=*p;
		*p=old;
		old=tmp;
		p++;
	}
}

static void insert_line(void)
{
	int oldtop,oldbottom;

	oldtop=top;
	oldbottom=bottom;
	top=y;
	bottom = video_num_lines;
	scrdown();
	top=oldtop;
	bottom=oldbottom;
}

static void delete_char(void)
{
	int i;
	unsigned char * p = (unsigned char *) pos;

	if (x>=video_num_columns)
		return;
	i = x;
	while (++i < video_num_columns) {
		*p = *(p+1);
		p++;
	}
	*p = video_erase_char;
}

static void delete_line(void)
{
	int oldtop,oldbottom;

	oldtop=top;
	oldbottom=bottom;
	top=y;
	bottom = video_num_lines;
	scrup();
	top=oldtop;
	bottom=oldbottom;
}

static void csi_at(unsigned int nr)
{
	if (nr > video_num_columns)
		nr = video_num_columns;
	else if (!nr)
		nr = 1;
	while (nr--)
		insert_char();
}

static void csi_L(unsigned int nr)
{
	if (nr > video_num_lines)
		nr = video_num_lines;
	else if (!nr)
		nr = 1;
	while (nr--)
		insert_line();
}

static void csi_P(unsigned int nr)
{
	if (nr > video_num_columns)
		nr = video_num_columns;
	else if (!nr)
		nr = 1;
	while (nr--)
		delete_char();
}

static void csi_M(unsigned int nr)
{
	if (nr > video_num_lines)
		nr = video_num_lines;
	else if (!nr)
		nr=1;
	while (nr--)
		delete_line();
}

static int saved_x=0;
static int saved_y=0;

static void save_cur(void)
{
	saved_x=x;
	saved_y=y;
}

static void restore_cur(void)
{
	gotoxy(saved_x, saved_y);
}

void con_write(struct tty_struct * tty)
{
	int nr;
	char c;

	nr = CHARS(tty->write_q);
	while (nr--) {
		c = getch(&tty->write_q);
		switch(state) {
			case 0:
				if (c>31 && c<127) {
					if (x>=video_num_columns) {
						x -= video_num_columns;
						pos -= video_size_row;
						lf();
					}
				/* SYMPLUS-PORT: M3 attr+char 双字节写 -> 单字符字节写
				 * （游戏屏纯 ASCII，颜色由 crt0 白字黑底设置提供） */
					*(unsigned char *)pos = (unsigned char)c;
					pos += 1;
					x++;
				} else if (c==27)
					state=1;
				else if (c==10 || c==11 || c==12)
					lf();
				else if (c==13)
					cr();
				else if (c==ERASE_CHAR(tty))
					del();
				else if (c==8) {
					if (x) {
						x--;
						pos -= 1;	/* SYMPLUS-PORT: M3 字节单元 */
					}
				} else if (c==9) {
					c=8-(x&7);
					x += c;
					pos += c;	/* SYMPLUS-PORT: M3 字节单元 */
					if (x>video_num_columns) {
						x -= video_num_columns;
						pos -= video_size_row;
						lf();
					}
					c=9;
				} else if (c==7)
					sysbeep();
				break;
			case 1:
				state=0;
				if (c=='[')
					state=2;
				else if (c=='E')
					gotoxy(0,y+1);
				else if (c=='M')
					ri();
				else if (c=='D')
					lf();
				else if (c=='Z')
					respond(tty);
				else if (x=='7')
					save_cur();
				else if (x=='8')
					restore_cur();
				break;
			case 2:
				for(npar=0;npar<NPAR;npar++)
					par[npar]=0;
				npar=0;
				state=3;
				if (ques=(c=='?'))
					break;
			case 3:
				if (c==';' && npar<NPAR-1) {
					npar++;
					break;
				} else if (c>='0' && c<='9') {
					par[npar]=10*par[npar]+c-'0';
					break;
				} else state=4;
			case 4:
				state=0;
				switch(c) {
					case 'G': case '`':
						if (par[0]) par[0]--;
						gotoxy(par[0],y);
						break;
					case 'A':
						if (!par[0]) par[0]++;
						gotoxy(x,y-par[0]);
						break;
					case 'B': case 'e':
						if (!par[0]) par[0]++;
						gotoxy(x,y+par[0]);
						break;
					case 'C': case 'a':
						if (!par[0]) par[0]++;
						gotoxy(x+par[0],y);
						break;
					case 'D':
						if (!par[0]) par[0]++;
						gotoxy(x-par[0],y);
						break;
					case 'E':
						if (!par[0]) par[0]++;
						gotoxy(0,y+par[0]);
						break;
					case 'F':
						if (!par[0]) par[0]++;
						gotoxy(0,y-par[0]);
						break;
					case 'd':
						if (par[0]) par[0]--;
						gotoxy(x,par[0]);
						break;
					case 'H': case 'f':
						if (par[0]) par[0]--;
						if (par[1]) par[1]--;
						gotoxy(par[1],par[0]);
						break;
					case 'J':
						csi_J(par[0]);
						break;
					case 'K':
						csi_K(par[0]);
						break;
					case 'L':
						csi_L(par[0]);
						break;
					case 'M':
						csi_M(par[0]);
						break;
					case 'P':
						csi_P(par[0]);
						break;
					case '@':
						csi_at(par[0]);
						break;
					case 'm':
						csi_m();
						break;
					case 'r':
						if (par[0]) par[0]--;
						if (!par[1]) par[1] = video_num_lines;
						if (par[0] < par[1] &&
						    par[1] <= video_num_lines) {
							top=par[0];
							bottom=par[1];
						}
						break;
					case 's':
						save_cur();
						break;
					case 'u':
						restore_cur();
						break;
				}
		}
	}
	set_cursor();
}

/*
 *  void con_init(void);
 *
 * This routine initalizes console interrupts, and does nothing
 * else. If you want the screen to clear, call tty_write with
 * the appropriate escape-sequece.
 *
 * Reads the information preserved by setup.s to determine the current display
 * type and sets everything accordingly.
 */
void con_init(void)
{
	register unsigned char a;

	/* SYMPLUS-PORT: M3 不读 0x90000 BIOS 参数区（游戏 RAM 无此数据），
	 * 硬编码游戏屏几何：ASCII 8 文本 96×40、帧缓冲 0x2000
	 * （与 symcc/include/config.h 的 FRAMEBUF_BASE/COLS/ROWS 一致） */
	video_num_columns = 96;
	video_size_row = video_num_columns;	/* 字节单元：每字符 1 字节（游戏屏纯 ASCII） */
	video_num_lines = 40;
	video_page = 0;
	video_erase_char = 0x20;	/* ' ' */
	video_mem_start = 0x2000;
	video_mem_end = video_mem_start + video_num_lines * video_size_row;
	video_port_reg = 0;		/* 无端口（outb_p/inb_p 为桩） */
	video_port_val = 0;
	video_type = VIDEO_TYPE_CGA;	/* 走非 EGA 简单滚屏路径（无 EGA 快速滚动） */
	
	/* SYMPLUS-PORT: M3 删除 BIOS 显示类型探测（游戏屏几何已硬编码） */

	/* SYMPLUS-PORT: M3 删除 display_desc 写屏（字节单元下会散写第 0 行） */
	/* Initialize the variables used for scrolling (mostly EGA/VGA)	*/
	
	origin	= video_mem_start;
	scr_end	= video_mem_start + video_num_lines * video_size_row;
	top	= 0;
	bottom	= video_num_lines;

	/* SYMPLUS-PORT: M3 起始光标 (0,0)（不读 ORIG_X/ORIG_Y） */
	gotoxy(0,0);
	set_trap_gate(0x21,&keyboard_interrupt);
	outb_p(inb_p(0x21)&0xfd,0x21);
	a=inb_p(0x61);
	outb_p(a|0x80,0x61);
	outb(a,0x61);
}
/* from bsd-net-2: */

void sysbeepstop(void)
{
	/* disable counter 2 */
	outb(inb_p(0x61)&0xFC, 0x61);
}

int beepcount = 0;

static void sysbeep(void)
{
	/* enable counter 2 */
	outb_p(inb_p(0x61)|3, 0x61);
	/* set command for counter 2, 2 byte write */
	outb_p(0xB6, 0x43);
	/* send 0x637 for 750 HZ */
	outb_p(0x37, 0x42);
	outb(0x06, 0x42);
	/* 1/8 second */
	beepcount = HZ/8;	
}











