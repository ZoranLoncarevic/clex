/*
 *
 * CLEX File Manager
 *
 * Copyright (C) 2001-2006 Vlado Potisk <vlado_potisk@clex.sk>
 *
 * CLEX is free software without warranty of any kind; see the
 * GNU General Public License as set out in the "COPYING" document
 * which accompanies the CLEX File Manager package.
 *
 * CLEX can be downloaded from http://www.clex.sk
 *
 */

#include <config.h>

#include <sys/types.h>	/* clex.h */
#include <ctype.h>		/* iscntrl() */
#include <stdarg.h>		/* va_list */
#include <string.h>		/* strcpy() */
#ifdef HAVE_NCURSES_H
# include <ncurses.h>	/* initscr() */
#else
# include <curses.h>
#endif
#ifdef HAVE_TERM_H
# include <term.h>		/* enter_bold_mode */
#else
# ifdef HAVE_NCURSES_TERM_H
#  include <ncurses/term.h>
# endif
#endif

#include "clex.h"
#include "inout.h"

#include "cfg.h"		/* config_num() */
#include "control.h"	/* get_current_mode() */
#include "edit.h"		/* edit_adjust() */
#include "panel.h"		/* pan_adjust() */
#include "sdstring.h"	/* SDSTR() */
#include "signals.h"	/* signal_initialize() */
#include "tty.h"		/* tty_press_enter() */
#include "userdata.h"	/* get_mylogin_at_host() */
#include "ustring.h"	/* USTR() */

#ifndef A_NORMAL
# define A_NORMAL 0
#endif

#ifndef A_BOLD
# define A_BOLD A_STANDOUT
#endif

#ifndef A_REVERSE
# define A_REVERSE A_STANDOUT
#endif

#ifndef A_UNDERLINE
# define A_UNDERLINE A_STANDOUT
#endif

#ifndef ACS_HLINE
# define ACS_HLINE '-'
#endif

/* safe addch() */
#define ADDCH(X)	addch(iscntrl(X) ? '?' | attrb : (X))

#define BLANK(X)	do { char_line(' ',X); } while (0)

static const char
	*layout_panel,		/* layout: file panel part */
	*layout_line;		/* layout: info line part */
static const char
	*info_remark = 0,	/* this remark is in the information line */
	*info_warnmsg = 0;	/* this warning is in the information line */

/* pos_xxx are used for win_position() control */
static CODE pos_resize = 0;	/* --( COLSxLINES )-- window size */
static CODE pos_wait = 0;	/* --< PLEASE WAIT >-- message */
static FLAG pos_panel = 0;	/* --< CURSOR/TOTAL >-- normal info */
						/* pos_resize, pos_wait:
						 *  2 = msg should be displayed
						 *  1 = msg is displayed and should be cleared
						 *  0 = msg is not displayed
						 * pos_panel:
						 *  1 = data changed --> update the screen
						 *  0 = no change
						 */ 
static int prevkey = 0;			/* previously pressed key */
static chtype attrr, attrb;		/* reverse and bold or substitutes */
static int framechar;			/* panel frame character
								  (note that ACS_HLINE is an int) */

static void win_info(void);		/* defined below */
static void win_position(void);	/* defined below */


static char type_symbol[][5] = {
	"    ", "exec", "suid", "Suid", "sgid", "/DIR", "/MNT",
	"Bdev", "Cdev", "FIFO", "sock", "spec", "  ??"
};	/* must correspond with  FT_XXX */

/* draw everything from scratch */
static void
screen_draw_all(void)
{
	int y, x;

	for (;/* until break */;) {
		clear();
		getmaxyx(stdscr,y,x);
		display.scrcols  = x;
		display.scrlines = y;
		display.pancols  = x - 4;	/* 2 + 2 colums for border */
		display.panlines = y - config_num(CFG_CMD_LINES) - 5;
		/*
		 * there are 2 special positions: the bottom-right corner
		 * is always left untouched to prevent automatic scrolling
		 * and the position just before it is reserved for the
		 * '>' continuation mark
		 */
		display.textline_area = x * config_num(CFG_CMD_LINES) - 2;
		if (x >= MIN_COLS && y >= MIN_LINES)
			break;
		/* window too small */
		printw("SCREEN: this window %d x %d is too small, "
		  "required is " STR(MIN_LINES) " x " STR(MIN_COLS)
		  " or larger; press ctrl-C to "
		  "exit the CLEX or (if possible) enlarge the window"
#ifndef KEY_RESIZE
		  " and press any other key to continue"
#endif
		  ". ",y,x);
		refresh();
		if (getch() == CH_CTRL('C'))
			err_exit("Window is too small");
	}
	win_heading();
	win_frame();
	win_bar();
	if (panel) {
		/* panel is NULL only when starting clex */
		pan_adjust(panel);
		win_panel();
		win_info();
		win_filter();
	}
	edit_adjust();
	win_edit();
}

/* start CURSES */
void
curses_initialize(void)
{
	if (TCLR(display.wait))
		tty_press_enter();

	initscr();			/* restores signal dispositions on FreeBSD ! */
	signal_initialize();	/* FreeBSD initscr() bug workaround ! */
	raw();
	nonl();
	noecho();
	keypad(stdscr,TRUE);
#ifdef HAVE_NOTIMEOUT
	notimeout(stdscr,TRUE);
#endif
	clear();
	refresh();
	display.curses = 1;

	if (enter_reverse_mode && *enter_reverse_mode)
		attrr = A_REVERSE;
	else
		attrr = A_STANDOUT;
	if (enter_bold_mode && *enter_bold_mode)
		attrb = A_BOLD;
	else if (enter_underline_mode && *enter_underline_mode)
		attrb = A_UNDERLINE;
	else
		attrb = A_STANDOUT;

	win_frame_reconfig();
	win_layout_reconfig();
	screen_draw_all();
}

/* restart CURSES */
void
curses_restart(void)
{
	if (TCLR(display.wait))
		tty_press_enter();

	reset_prog_mode();
	touchwin(stdscr);
	display.curses = 1;
	screen_draw_all();
}

/* stop CURSES */
/* note: this is a cleanup function */
void
curses_stop(void)
{
	clear();
	refresh();
	endwin();
	display.curses = 0;
}

/* stop CURSES */
/* this is a version that also issues smcup */
void
curses_stop_with_smcup(void)
{
	curses_stop();
	fputs(tgetstr("ti",NULL),stdout);
	fflush(stdout);
}

/* set cursor to the proper position and refresh screen */
static void
screen_refresh(void)
{
	int pos;

	if (pos_wait || pos_resize || pos_panel)
		win_position();		/* display/clear message */

	if (panel->filtering == 1)
		move(display.panlines + 2,13 + panel->filter->curs);
	else {
		if (textline == 0)
			pos = 0;
		else if (textline->offset)
			pos = 1 + textline->curs - textline->offset;
		else
			pos = textline->curs + textline->promptlen;
		move(display.panlines + 5 + pos / display.scrcols,
		  pos % display.scrcols);
	}
	refresh();
}

/*
 * like printf() but curses mode is suspended if necessary,
 * txt_printf() is to be used in xxx_reconfig() functions
 * which may be invoked in both curses and non-curses modes.
 *
 * after calling txt_printf() at least once, normal printf() may
 * be used as well
 */
void
txt_printf(const char *format, ...)
{
	va_list argptr;

	display.wait = 1;
	if (display.curses) {
		curses_stop();
		puts("\nReconfiguring the CLEX");
	}

	va_start(argptr,format);
	vprintf(format,argptr);
	va_end(argptr);
	fflush(stdout);
}

/****** keyboard input functions ******/

/*
 * get next input char, no processing except screen resize event,
 * use this input function to get unfiltered input
 */
int
kbd_getraw(void)
{
	int key, retries;

	for (;/* until return */;) {
		screen_refresh();
		retries = 10;
		do {
			if (--retries < 0)
				err_exit("Cannot read the keyboard input");
			key = getch();
		} while (key == ERR);
#ifdef KEY_RESIZE
		if (key == KEY_RESIZE) {
			pos_resize = 2;
			screen_draw_all();
			continue;
		}
#endif
#ifdef KEY_SUSPEND
		if (key == KEY_SUSPEND)
			return CH_CTRL('Z');
#endif
		if (key == KEY_BACKSPACE)
			return CH_CTRL('H');
		if (key == KEY_ENTER)
			return CH_CTRL('M');
		if (key == CH_CTRL('G'))
			return CH_CTRL('C');
		if (key == KEY_DC)
			return '\177';
		if (key == KEY_IL)
			return KEY_IC;
		return key;
	}
}

/*
 * get next input char, not saved for kbd_esc() usage,
 * use this function to get a single key unrelated to the rest
 * of input stream (e.g. "press any key to continue")
 */
static int
kbd_getany(void)
{
	int key;

	while ((key = kbd_getraw()) == CH_CTRL('L'))
		/* redraw screen */
		wrefresh(curscr);
	return key;
}

/* get next input char */
int
kbd_input(void)
{
	int ch;
	static int key = 0;

	/* show ASCII code */
	if (info_remark == 0 && textline != 0) {
		ch = (unsigned char)USTR(textline->line)[textline->curs];
		if (ch != '\0' && iscntrl(ch)) {
			if (ch >= 1 && ch <= 26)
				win_remark_fmt("Ctrl-%c  ASCII %d",ch + 'A' - 1,ch);
			else
				win_remark_fmt("ASCII %d",ch);
		}
	}

	prevkey = key;
	key = kbd_getany();	
	/* <esc> 1 --> <F1>, <esc> 2 --> <F2>, ... <esc> 0 --> <F10> */
	if (prevkey == CH_ESC && IS_CHAR(key) && isdigit(key)) {
		prevkey = 0;
		key = KEY_F((key - '0' + 9) % 10 + 1);
	}

	/* dismiss remark (if any) */
	if (info_remark) {
		info_remark = 0;
		win_info();
	}

	return key;
}

/* was the previous key an ESC ? */
int
kbd_esc(void)
{
	return prevkey == CH_ESC;
}

/****** output functions ******/

/*
 * following functions write to these screen areas:
 *
 * win_heading							/usr/local
 * win_frame							----------
 * win_panel							 DIR bin
 * win_panel							>DIR etc <
 * win_panel							 DIR man
 * win_frame, win_filter,
 * win_waitmsg, win_position			-< filt >----< 3/9 >-
 * win_info, win_warning, win_remark	0755 rwxr-xr-x
 * win_bar								[ CLEX file manager ]
 * win_edit								shell $ ls -l_
 */

void
win_frame_reconfig(void)
{
	switch(config_num(CFG_FRAME)) {
	case 0:
		framechar = '-';
		break;
	case 1:
		framechar = '=';
		break;
	default:
		framechar = ACS_HLINE;
	}
}

void
win_layout_reconfig(void)
{
	static USTRING layout = { 0,0 };
	FLAG fld;
	char ch, *pch;

	us_copy(&layout,config_layout);

	/* split layout to panel fields and line fields */
	layout_panel = USTR(layout);
	for (fld = 0, pch = USTR(layout); (ch = *pch); pch++)
		if (!TCLR(fld)) {
			if (ch == '$')
				fld = 1;
			else if (ch == '|') {
				*pch = '\0';
				layout_line = pch + 1;
				return;	/* success */
			}
		}

	layout_line = "$m $p $o";
	txt_printf("CONFIG: Incorrect LAYOUT syntax: missing bar '|'\n");
}

/* write a line of repeating chars */
static void
char_line(int ch, int cnt)
{
	while (cnt-- > 0)
		addch(ch);
}

void
win_frame(void)
{
	move(1,0);
	char_line(framechar,display.scrcols);
	move(display.panlines + 2,0);
	char_line(framechar,display.scrcols);
}

static void
print_position(const char *msg, int len, int bold)
{
	static int prev_len = 0;

	if (prev_len > len) {
		move(display.panlines + 2,display.scrcols - prev_len - 2);
		char_line(framechar,prev_len - len);
	}
	else
		move(display.panlines + 2,display.scrcols - len - 2);
	if (bold)
		attrset(A_BOLD);
	addstr(msg);
	if (bold)
		attrset(A_NORMAL);
	prev_len = len;
}

static void
win_position(void)
{
	char buffer[48];
	int len;

	if (pos_resize == 2) {
		len = sprintf(buffer,"( %dx%d )",
		  display.scrcols,display.scrlines);
		print_position(buffer,len,1);
		pos_resize = 1;
		return;
	}
		
	if (pos_wait == 2) {
		print_position("< PLEASE WAIT >",15,1);
		pos_wait = 1;
		return;
	}

	pos_wait = pos_resize = pos_panel = 0;

	if (panel->cnt == 0) {
		print_position("< NO DATA >",11,1);
		return;
	}

	if (panel->curs < 0) {
		print_position("",0,0);
		return;
	}

	if (panel->type == PANEL_TYPE_FILE && ppanel_file->selected)
		len = sprintf(buffer,"< [%d] %d/%d >",
		  ppanel_file->selected,panel->curs + 1,panel->cnt);
	else
		len = sprintf(buffer,"< %d/%d >",panel->curs + 1,panel->cnt);
	print_position(buffer,len,0);
}

void
win_waitmsg(void)
{
	if (display.curses && pos_wait == 0) {
		pos_wait = 2;
		screen_refresh();
	}
}

void
win_filter(void)
{
	int len;

	len = INPUT_STR + 12;
	move(display.panlines + 2,2);
	if (panel->filtering) {
		if (panel->type == PANEL_TYPE_FILE && ppanel_file->filtype)
			addstr("< pattern: ");	/* 11 */
		else
			addstr("<  filter: ");	/* 11 */
		attrset(attrb);
		addstr(panel->filter->line);
		attrset(A_NORMAL);
		addstr(" >");			/* 2 */
		len -= 13 + panel->filter->size;
	}
	char_line(framechar,len);
}

/*
 * putstr_trunc() writes string 'str' padding or truncating it
 * to the total length of 'maxwidth'. Its behavior might be altered
 * by OPT_XXX options (may be OR-ed together)
 *
 * putstr_trunc() returns the total number of characters written
 * (only useful with OPT_NOPAD)
 */
#define OPT_NOPAD	1	/* do not pad */
#define OPT_NOCONT	2	/* do not write continuation mark '>' */
#define OPT_SQUEEZE	4	/* squeeze long str in the middle: abc...xyz */
static int
putstr_trunc(const char *str, int maxwidth, int options)
{
	int i, ch, len, dots, part1, part2;

	if (maxwidth <= 0)
		return 0;

	if (options & OPT_SQUEEZE) {
		len = strlen(str);
		if (len > maxwidth) {
			dots = maxwidth >= 6 ? 4 : 1;
			part2 = 5 * (maxwidth - dots) / 8;
			part1 = maxwidth - dots - part2;
			putstr_trunc(str,part1,OPT_NOCONT);
			char_line('.',dots);
			putstr_trunc(str + len - part2,part2,0);
			return maxwidth;
		}
		/* else: SQUEEZE option is superfluous --> ignored */
	}

	for (i = 0; i < maxwidth - 1; i++)
		if ( (ch = (unsigned char)str[i]) )
			ADDCH(ch);
		else {
			/* EOL */
			if (options & OPT_NOPAD)
				return i;
			BLANK(maxwidth - i);
			return maxwidth;
		}

	ch = (unsigned char)str[i];
	if (ch == '\0') {
		if (options & OPT_NOPAD)
			return i;
		addch(' ');
	}
	else if (str[i + 1] && !(options & OPT_NOCONT))
		addch('>' | attrb);	/* continuation mark in bold font */
	else
		ADDCH(ch);
	return maxwidth;
}

/* file panel heading: primary + secondary panel's directory */
static void
twodirs(void)
{
	int len1, len2, opt1, opt2, width;
	const char *dir1, *dir2;

	/*
	 * directory names are separated by 2 spaces
	 * their width is kept in ratio 5:3
	 */
	width = display.scrcols - 2;				/* available room */
	len1 = strlen(dir1 = USTR(ppanel_file->dir));
	len2 = strlen(dir2 = USTR(ppanel_file->other->dir));
	opt1 = opt2 = 0;
	if (len1 + len2 <= width)
		len1 = width - len2;					/* enough room */
	else if (len1 <= (5 * width) / 8) {
		len2 = width - len1;					/* squeeze second */
		opt2 = OPT_SQUEEZE;
	}
	else if (len2 <= (3 * width) / 8) {
		len1 = width - len2;					/* squeeze first */
		opt1 = OPT_SQUEEZE;
	}
	else {
		len1 = (5 * width) / 8;					/* squeeze both */
		len2 = width - len1;
		opt1 = opt2 = OPT_SQUEEZE;
	}
	attrset(attrb);
	putstr_trunc(dir1,len1,opt1);
	attrset(A_NORMAL);
	addstr("  ");
	putstr_trunc(dir2,len2,opt2);
}

/* panel heading - top screen line */
void
win_heading(void)
{
	int mode, len;
	const char *msg;

	move(0,0);

	mode = get_current_mode();
	if (mode == MODE_FILE) {
		twodirs();
		return;
	}

	addch(' ');
	len = 1;
	switch (mode) {
	case MODE_BM_EDIT:
		msg = "BOOKMARK MANAGER > EDIT";
		break;
	case MODE_BM_LIST:
		msg = "CHANGE WORKING DIRECTORY > BOOKMARKS";
		break;
	case MODE_BM_MANAGER:
		msg = "BOOKMARK MANAGER  |  U/D = up/down, <insert> or I, <del> or R, <enter> = edit";
		break;
	case MODE_CFG:
		msg = "CONFIGURATION  |  <enter> = change, O = original, S = standard";
		break;
	case MODE_CFG_EDIT_NUM:
	case MODE_CFG_EDIT_TXT:
		msg = "CONFIGURATION > EDIT";
		break;
	case MODE_COMPARE:
		msg = "DIRECTORY COMPARE";
		break;
	case MODE_COMPL:
		msg = panel_compl.description;
		addstr(msg);
		len += strlen(msg);
		msg = " COMPLETION";
		break;
	case MODE_DIR:
	case MODE_DIR_SPLIT:
		msg = "CHANGE WORKING DIRECTORY";
		break;
	case MODE_GROUP:
		msg = "GROUP INFORMATION";
		break;
	case MODE_HELP:
		addstr("HELP: ");
		len = 7;
		attrset(attrb);
		msg = panel_help.heading;
		break;
	case MODE_HIST:
		msg = "COMMAND HISTORY  |  <tab> = insert, <esc> <del> = delete";
		break;
	case MODE_MAINMENU:
		msg = "MAIN FUNCTION MENU";
		break;
	case MODE_PASTE:
		msg = "COMPLETE/INSERT NAME";
		break;
	case MODE_SELECT:
	case MODE_DESELECT:
		msg = "DESELECT FILES";
		if (mode == MODE_SELECT)
			msg += 2;	/* DESELECT... --> SELECT ... */
		break;
	case MODE_SORT:
		msg = "SORT ORDER";
		break;
	case MODE_USER:
		msg = "USER INFORMATION";
		break;
	default:
		/* mode not set yet (during start-up) */
		msg = "CLEX";
	}
	putstr_trunc(msg,display.scrcols - len,0);
	if (mode == MODE_HELP)
		attrset(A_NORMAL);
}

/* "0644" -> "rw-r--r--" */
static void
print_perms(const char *octal)
{
	static const char
		*set1[8] =
			{ "---","--x","-w-","-wx","r--","r-x","rw-","rwx" },
		*set2[8] =
			{ "--S","--s","-wS","-ws","r-S","r-s","rwS","rws" },
		*set3[8] =
			{ "--T","--t","-wT","-wt","r-T","r-t","rwT","rwt" };

	addstr(((octal[0] - '0') & 4 ? set2 : set1)[(octal[1] - '0') & 7]);
	addstr(((octal[0] - '0') & 2 ? set2 : set1)[(octal[2] - '0') & 7]);
	addstr(((octal[0] - '0') & 1 ? set3 : set1)[(octal[3] - '0') & 7]);
}

/*
 * see CFG_LAYOUT1 for more information about 'fields',
 * function returns the remaining unused width
 */
static int
print_fields(FILE_ENTRY *pfe, int width, const char *fields)
{
	const char *txt;
	FLAG fld, left_align;
	int ch, i, fw;
	static char field[3] ="$x";

	for (fld = left_align = 0; width > 0 && (ch = (unsigned char)*fields++); ) {
		if (!TCLR(fld)) {
			if (ch == '$')
				fld = 1;
			else {
				addch(ch);
				width--;
				/* choose proper alignment (left or right) */
				left_align = (ch != ' ');
			}
		}
		else {
			switch (ch) {
			case 'a':	/* access date/time */
				fw = display.date_len;
				txt = pfe->atime_str;
				break;
			case 'd':	/* modification date/time */
				fw = display.date_len;
				txt = pfe->mtime_str;
				break;
			case 'i':	/* inode change date/time */
				fw = display.date_len;
				txt = pfe->ctime_str;
				break;
			case 'l':	/* links (total number) */
				fw = FE_LINKS_STR - 1;
				txt = pfe->links_str;
				break;
			case 'L':	/* links (flag) */
				fw = 3;
				txt = pfe->links ? "LNK" : "   ";
				break;
			case 'm':	/* file mode */
				fw = FE_MODE_STR - 1;
				txt = pfe->mode_str;
				break;
			case 'M':	/* file mode (alternative format) */
				fw = FE_MODE_STR - 1;
				txt = pfe->normal_mode ? "" : pfe->mode_str;
				break;
			case 'o':	/* owner */
				fw = FE_OWNER_STR - 1;
				txt = pfe->owner_str;
				break;
			case 'p':	/* permissions */
				fw = 9;	/* rwxrwxrwx */
				txt = 0;
				break;
			case 'P':	/* permissions (alternative format) */
				fw = 9;
				txt = pfe->normal_mode ? "" : 0;
				break;
			case 's':	/* file size (device major/minor) */
				fw = FE_SIZE_DEV_STR - 1;
				txt = pfe->size_str;
				break;
			case 'S':	/* file size (not for directories) */
				fw = FE_SIZE_DEV_STR - 1;
				txt = IS_FT_DIR(pfe->file_type) ? "" : pfe->size_str;
				break;
			case 't':	/* file type */
				fw = 4;
				txt = type_symbol[pfe->file_type];
				break;
			case '>':	/* symbolic link */
				fw = 2;
				txt = pfe->symlink ? "->" : "  ";
				break;
			case '*':	/* selection mark */
				fw = 1;
				txt = pfe->select ? "*" : " ";
				break;
			case '$':	/* literal $ */
				fw = 1;
				txt = "$";
				break;
			case '|':	/* literal | */
				fw = 1;
				txt = "|";
				break;
			default:	/* syntax error */
				fw = 2;
				field[1] = ch;
				txt = field;
			}

			if (width < fw)
				break;

			/*
             * txt == NULL     - compute the string
             * txt == ""       - leave the field blank
             * txt == "string" - print this string
             */
			if (txt == 0) {
				/* $p */
				if (pfe->file_type != FT_NA)
					print_perms(pfe->mode_str);
				else
					BLANK(fw);
			}
			else if (*txt == '\0')
				BLANK(fw);
			else if (left_align && *txt == ' ') {
				/* change alignment from right to left */
				for (i = 1; txt[i] == ' '; i++)
					;
				addstr(txt + i);
				BLANK(i);
			}
			else
				addstr(txt);

			width -= fw;
		}
	}

	return width;
}

static void
win_info_extra_line(int ln)
{
	const char *msg;

	msg = panel->extra[ln].info;
	if (msg) {
		if (msg[0] == '@') {
			/* special handling */
			addstr("  working directory is "); /* 23 */
			putstr_trunc(USTR(ppanel_file->dir),display.scrcols - 23,0);
		}
		else {
			addstr("  ");
			putstr_trunc(msg,display.scrcols - 2,0);
		}
	}
	else
		clrtoeol();
}

static void
pfe_info(FILE_ENTRY *pfe)
{
	if (pfe->file_type == FT_NA)
		addstr("no status information available");
	else if (print_fields(pfe,display.scrcols,layout_line) == 0)
		return;
	clrtoeol();
}

/* information line */
static void
win_info(void)
{
	int width;
	const char *msg;

	move(display.panlines + 3,0);

	/* warnmsg has greater priority than a remark */
	if (info_warnmsg) {
		flash();
		attrset(attrb);
		width =
		  putstr_trunc(info_warnmsg,display.scrcols - 15,OPT_NOPAD);
		attrset(A_NORMAL);
		addstr(" Press any key.");		/* 15 */
		if (display.scrcols - width > 15)
			clrtoeol();
		return;
	}

	if (info_remark) {
		attrset(attrb);
		addstr("-- ");					/* 3 */
		width = putstr_trunc(info_remark,display.scrcols - 6,OPT_NOPAD);
		addstr(" --");					/* 3 */
		attrset(A_NORMAL);
		if (display.scrcols - width > 6)
			clrtoeol();
		return;
	}

	if (panel->curs < 0 && panel->min < 0) {
		/* extra panel lines */
		win_info_extra_line(panel->curs - panel->min);
		return;
	}

	if (panel->cnt == 0) {
		clrtoeol();
		return;
	}

	switch (panel->type) {
		case PANEL_TYPE_CFG:
			addstr("  ");					/* 2 */
			msg = panel_cfg.config[panel->curs].help;
			putstr_trunc(msg,display.scrcols - 2,0);
			break;
		case PANEL_TYPE_COMPL:
			if ( (msg = panel_compl.candidate[panel->curs].aux) ) {
				addstr("  additional data: ");		/* 19 */
				putstr_trunc(msg,display.scrcols - 19,0);
			}
			else
				clrtoeol();
			break;
		case PANEL_TYPE_FILE:
			pfe_info(ppanel_file->files[panel->curs]);
			break;
		default:
			clrtoeol();
	}
}

/* warning message - can be used also in text mode */
void
win_warning(const char *msg)
{
	if (!display.curses) {
		if (get_current_mode() == 0)
			/* startup */
			display.wait = 1;
		else {
			/* directory re-read after cmd execution */
			putchar('\n');
			fflush(stdout);
		}
		puts(msg);
		return;
	}

	/*
	 * win_info() is responsible for this screen area,
	 * real work is performed there
	 */
	info_warnmsg = msg;
	win_info();
	kbd_getany();
	info_warnmsg = 0;
	win_info();
}

void
win_warning_fmt(const char *format, ...)
{
	va_list argptr;
	static char buff[160];

	va_start(argptr,format);
	vsnprintf(buff,sizeof(buff),format,argptr);
	va_end(argptr);
	win_warning(buff);
}

void
win_remark_fmt(const char *format, ...)
{
	va_list argptr;
	static char buff[160];

	if (!display.curses) {
		txt_printf("BUG: win_remark_fmt(\"%s\",...)\n"
		  "     called in text mode\n",format);
		return;
	}

	va_start(argptr,format);
	vsnprintf(buff,sizeof(buff),format,argptr);
	va_end(argptr);
	info_remark = buff;
	win_info();
}

/* simplified version, 'str' must not be an automatic variable */
void
win_remark(const char *str)
{
	if (!display.curses) {
		txt_printf("BUG: win_remark(\"%s\")\n"
		  "     called in text mode\n",str);
		return;
	}

	info_remark = str;
	win_info();
}

void
win_completion(int cnt, const char *chars)
{
	int i;
	char buffer[300], *dst;

	if (chars == 0) {
		win_remark_fmt("%d completion possibilities, "
		  "press <TAB> again for more info",cnt);
		return;
	}

	dst = buffer;
	if (chars[0]) {
		strcpy(dst,"<none>");		/* 6 */
		dst += 6;
	}
	for (i = 1; i < 256; i++)
		if (chars[i])
			*dst++ = i;
	*dst = '\0';

	win_remark_fmt("next char: %s",buffer);
}

void
win_bar(void)
{
	int len;

	attrset(attrr);
	move(display.panlines + 4,0);
	len = strlen(clex_data.login_at_host) + 2;	/* + 2 spaces */
	addstr(" CLEX file manager - ");			/* 21 */
	putstr_trunc(clex_data.admin ? "ADMIN MODE" :
	  "alt-M for menu, F1 for help",display.scrcols - 21 - len,0);
	addch(' ');
	putstr_trunc(clex_data.login_at_host,len + 1,0);
	attrset(A_NORMAL);
}

void
win_edit(void)
{
	int width, offset;

	move(display.panlines + 5,0);

	/* special case: no textline */
	if (textline == 0) {
		clrtobot();
		return;
	}

	/* special case: empty line (prompt not highlighted) */
	if (textline->size == 0) {
		addstr(textline->prompt);
		clrtobot();
		return;
	}

	/* prompt (or continuation mark) */
	offset = textline->offset;
	width = display.textline_area;
	if (offset == 0) {
		if ((panel->type != PANEL_TYPE_DIR_SPLIT
		  && panel->type != PANEL_TYPE_DIR) || panel->norev)
		attrset(attrb);
		addstr(textline->prompt);
		width -= textline->promptlen;
	}
	else {
		attrset(attrb);
		addch('<');
		width--;
	}
	attrset(A_NORMAL);
	putstr_trunc(USTR(textline->line) + offset,width + 1,0);
}

/****** win_panel() and friends  ******/

static void
draw_line_bm(int ln)
{
	putstr_trunc(USTR(panel_bm_lst.bm[ln]),display.pancols,0);
}

static void
draw_line_cfg(int ln)
{
	putstr_trunc(panel_cfg.config[ln].var,CFGVAR_LEN,0);
	addstr(" = ");			/* 3 */
	putstr_trunc(config_print_value(ln),display.pancols - CFGVAR_LEN - 3,0);
}

static void
draw_line_compare(int ln)
{
	static const char *description[] = {
		"0: name, type       (symbolic links ok)",
		"1: name, type, size (symbolic links ok)",
		"2: name, type, size",
		"3: name, type, size, ownership+permissions",
		"4: name, type, size, contents",
		"5: name, type, size, ownership+permissions, contents",
	};

	putstr_trunc(description[ln],display.pancols,0);
}

static void
draw_line_compl(int ln)
{
	COMPL_ENTRY *pcc;

	pcc = panel_compl.candidate + ln;
	if (panel_compl.filenames) {
		addstr(pcc->is_link ? "-> " : "   " );	/* 3 */
		addstr(type_symbol[pcc->file_type]);	/* 4 */
		addstr("  ");							/* 2 */
	}
	else
		BLANK(9);
	putstr_trunc(SDSTR(pcc->str),display.pancols - 9,0);
}

static void
draw_line_dir(int ln)
{
	int shlen;

	if ((shlen = panel_dir.dir[ln].shlen)
	  && (ln == panel_dir.pd->top || shlen >= display.pancols))
		shlen = 0;
	if (shlen) {
		BLANK(shlen - 2);
		addstr("__");
	}
	putstr_trunc(panel_dir.dir[ln].name + shlen,
	  display.pancols - shlen,0);
}

static void
draw_line_dir_split(int ln)
{
	int len;

	len = panel_dir_split.dir[ln].shlen;
	if (len > display.pancols)
		putstr_trunc(panel_dir_split.dir[ln].name,display.pancols,0);
	else {
		putstr_trunc(panel_dir_split.dir[ln].name,len,OPT_NOCONT);
		BLANK(display.pancols - len);
	}
}

static void
draw_line_file(int ln)
{
	FILE_ENTRY *pfe;
	int width;

	pfe = ppanel_file->files[ln];
	if (pfe->select)
		attron(attrb);

	/* 10 columns reserved for the filename */
	width = 10 + print_fields(pfe,display.pancols - 10,layout_panel);
	if (!pfe->symlink)
		putstr_trunc(SDSTR(pfe->file),width,0);
	else {
		width -= putstr_trunc(SDSTR(pfe->file),width,OPT_NOPAD);
		width -= putstr_trunc(" -> ",width,OPT_NOPAD);
		putstr_trunc(USTR(pfe->link),width,0);
	}

	if (pfe->select)
		attroff(attrb);
}

static void
draw_line_grp(int ln)
{
	printw("%10u  ",(unsigned int)panel_group.groups[ln].gid);
	putstr_trunc(panel_group.groups[ln].group,display.pancols - 12,0);
}

static void
draw_line_help(int ln)
{
	FLAG link;

	if ( (link = panel_help.line[ln].aux != 0) )
		attron(attrb);
	putstr_trunc(panel_help.line[ln].txt,display.pancols,0);
	if (link)
		attroff(attrb);
}

static void
draw_line_hist(int ln)
{
	if (panel_hist.hist[ln]->failed)
		addstr("failed: ");		/* 8 */
	else 
		BLANK(8);
	putstr_trunc(USTR(panel_hist.hist[ln]->cmd),display.pancols - 8,0);
}

static void
draw_line_mainmenu(int ln)
{
	static const char *description[] = {
		"help                                     <F1>",
		"change working directory                 alt-W",
		"  change into root directory             alt-/",
		"  change into parent directory           alt-.",
		"  change into home directory             alt-~",
		"  bookmarks                              alt-K",
		"command history                          alt-H",
		"sort order for filenames                 alt-S",
		"re-read current directory                ctrl-R",
		"compare directories                      alt-=",
		"filter on/off                            ctrl-F",
		"user (group) information                 alt-U (alt-G)",
		"select files:  select all                (alt-M) +",
		"               deselect all              (alt-M) -",
		"               select using pattern      alt-+",
		"               deselect using pattern    alt--",
		"               invert selection          alt-*",
		"configure CLEX                           alt-C",
		"program version                          alt-V",
		"quit                                     alt-Q"
		/* must correspond with tab_mainmenu[] in control.c */
	};

	putstr_trunc(description[ln],display.pancols,0);
}

static void
draw_line_pastemenu(int ln)
{
	static const char *description[] = {
		"complete name (auto)",
		"complete filename - any type",
		"complete filename - directory",
		"complete filename - executable",
		"complete username",
		"complete environment variable name",
		"complete command from the command history         alt-P",
		"insert current filename                           <F2>",
		"insert all selected filenames               <esc> <F2>",
		"insert full pathname of current file              ctrl-A",
		"insert secondary working directory name           ctrl-E",
		"insert current working directory name       <esc> ctrl-E",
		"insert the target of a symbolic link              ctrl-O"
		/* must correspond with tab_pastemenu[] in control.c */
	};
	putstr_trunc(description[ln],display.pancols,0);
}

static void
draw_line_sort(int ln)
{
	static const char *description[] = {
		"name",
		".extension",
		"size [small -> large]",
		"size [large -> small]",
		"time of last modification [recent -> old]",
		"time of last modification [old -> recent]",
		"reversed name (use in sendmail queue directory)"
		/* must correspond with SORT_XXX */
	};

	addstr(panel_sort.order == ln ? "(x" : "( ");	/* 2 */
	addstr(") sort by ");							/* 10 */
	putstr_trunc(description[ln],display.pancols - 12,0);
}

static void
draw_line_usr(int ln)
{
	int len;

	printw("%10u  ",(unsigned int)panel_user.users[ln].uid);
	len = putstr_trunc(panel_user.users[ln].login,48,OPT_NOPAD);
	if (len < 14) {
		BLANK(14 - len);
		len = 14;
	}
	addstr(" ");
	putstr_trunc(panel_user.users[ln].gecos,display.pancols - len - 13,0);
}

static void
draw_panel_extra_line(int ln)
{
	const char *msg;
	FLAG help;

	help = panel->type == PANEL_TYPE_HELP;
	if (help) {
		attrset(attrb);
		addstr("==> ");			/* 4 */
	}
	else
		addstr("--> ");			/* 4 */
	msg = panel->extra[ln].text;
	putstr_trunc(msg ? msg : "Leave this panel",display.pancols - 4,0);
	if (help)
		attrset(A_NORMAL);
}

static void
draw_panel_line(int y)
{
	int ln;
	/* must correspond with PANEL_TYPE_XXX */
	static void (*draw_line[])(int) = {
	  draw_line_bm, draw_line_cfg, draw_line_compare, draw_line_compl,
	  draw_line_dir, draw_line_dir_split, draw_line_file, draw_line_grp,
	  draw_line_help, draw_line_hist, draw_line_mainmenu,
	  draw_line_pastemenu, draw_line_sort, draw_line_usr
	};

	move(2 + y,0);
	ln = panel->top + y;
	if (ln >= panel->cnt) {
		clrtoeol();
		return;
	}

	if (panel->curs == ln) {
		addch('>');
		if (!panel->norev)
			attrset(attrr);
		addch(' ');
	}
	else
		addstr("  ");
	if (ln < 0)
		draw_panel_extra_line(ln - panel->min);
	else
		(*draw_line[panel->type])(ln);
	if (panel->curs == ln) {
		addch(' ');
		attrset(A_NORMAL);
		addch('<');
	}
	else
		addstr("  ");
}

static void
draw_panel(int optimize)
{
	static int save_top, save_curs, save_ptype = PANEL_TYPE_NONE;
	int y;

	if (panel->type != save_ptype) {
		/* panel type has changed */
		optimize = 0;
		save_ptype = panel->type;
	}

	if (optimize && save_top == panel->top) {
		/* redraw only the old and new current lines */
		draw_panel_line(save_curs - panel->top);
		if (save_curs != panel->curs) {
			pos_panel = 1;
			draw_panel_line(panel->curs - panel->top);
			save_curs = panel->curs;
		}
		else if (panel->type ==  PANEL_TYPE_FILE)
			/* number of selected files could have changed */
			pos_panel = 1;
	}
	else {
		pos_panel = 1;
		/* redraw all lines */
		for (y = 0; y < display.panlines; y++)
			draw_panel_line(y);
		save_top = panel->top;
		save_curs = panel->curs;
	}

	win_info();
}

/* win_panel() without optimization */
void
win_panel(void)
{
	draw_panel(0);
}

/*
 * win_panel() with optimization
 *
 * use this win_panel() version if the only change made since last
 * win_panel() call is a cursor movement or a modification of the
 * current line
 */
void
win_panel_opt(void)
{
	draw_panel(1);
}
