/*
 *
 * CLEX File Manager
 *
 * Copyright (C) 2001-2006 Vlado Potisk <vlado_potisk@clex.sk>
 * Copyright (C) 2016 Zoran Loncarevic <zoran233@gmail.com>
 *
 * CLEX is free software without warranty of any kind; see the
 * GNU General Public License as set out in the "COPYING" document
 * which accompanies the CLEX File Manager package.
 *
 * CLEX can be downloaded from http://www.clex.sk
 *
 */

#include <config.h>

#include <sys/types.h>		/* clex.h */
#include <string.h>			/* strchr() */

#include "clex.h"
#include "edit.h"

#include "cfg.h"			/* config_str() */
#include "filepanel.h"		/* cx_files_enter() */
#include "inout.h"			/* win_edit() */
#include "history.h"		/* hist_reset_index() */
#include "sdstring.h"		/* SDSTR() */
#include "undo.h"			/* undo_before() */
#include "ustring.h"		/* USTR() */

#define OFFSET_STEP	16
#define ROUND_UP(X) ((X + OFFSET_STEP - 1) / OFFSET_STEP * OFFSET_STEP)
#define ROUND_DOWN(X) ((X) / OFFSET_STEP * OFFSET_STEP)

FLAG specialF = 0;

/* adjust 'offset' so the cursor is visible */
int
edit_adjust(void)
{
	int old_offset, offset, screen, delta;

	if (textline == 0)
		return 0;

	old_offset = offset = textline->offset;
	/* screen = maximum number of characters that fit on the screen */
	screen = display.textline_area;

	/* handle cursor too far left */
	if (offset > textline->curs)
		offset = ROUND_DOWN(textline->curs);

	if (offset) {
		/*
		 * delta = space left blank
		 * eliminate it as much as possible
		 */
		delta = screen - (1 /* > mark */ + textline->size - offset);
		if (delta >= OFFSET_STEP) {
			offset -= ROUND_DOWN(delta);
			LIMIT_MIN(offset,0);
		}
	}

	if (textline->curs != textline->size)
		/* reserve one position for the possible continuation mark > */
		screen--;

	/* handle cursor too far right (with the regular prompt) */
	if (offset == 0 && textline->promptlen + textline->curs > screen)
		offset = OFFSET_STEP;	/* will be handled below */
	/* handle cursor too far right (with the continuation mark '>') */
	if (offset) {
		/*
		 * delta (if positive) = cursor position excess
		 * eliminate it totaly
		 */
		delta = 1 + textline->curs - offset - screen;
		if (delta > 0)
			offset += ROUND_UP(delta);
	}

	return old_offset != (textline->offset = offset);
}

/* make changes to 'textline' visible on the screen */
void
edit_update(void)
{
	edit_adjust();
	win_edit();
}

/*
 * if you have only moved the cursor, use this optimized
 * version of edit_update() instead
 */
void
edit_update_cursor(void)
{
	if (edit_adjust())
		win_edit();
}

/* returns 1 if the line is too long to fit on the screen */
int
edit_islong(void)
{
	return textline->offset ||
	  textline->promptlen + textline->size > display.textline_area + 1;
}

void
cx_edit_begin(void)
{
	textline->curs = 0;
	edit_update_cursor();
}

void
cx_edit_end(void)
{
	textline->curs = textline->size;
	edit_update_cursor();
}

void
cx_edit_left(void)
{
	if (!textline->size) cx_files_cd_parent();
	if (textline->curs > 0) {
		textline->curs--;
		edit_update_cursor();
	}
}

void
cx_edit_right(void)
{
	if (!textline->size) cx_files_enter();
	if (textline->curs < textline->size) {
		textline->curs++;
		edit_update_cursor();
	}
}

void
cx_edit_up(void)
{
	textline->curs -= display.scrcols;
	LIMIT_MIN(textline->curs,0);
	edit_update_cursor();
}

void
cx_edit_down(void)
{
	textline->curs += display.scrcols;
	LIMIT_MAX(textline->curs,textline->size);
	edit_update_cursor();
}

/* move one word left */
void
cx_edit_w_left_(void)
{
	const char *line;

	if (textline->curs > 0) {
		line = USTR(textline->line);
		while (textline->curs > 0 && line[textline->curs - 1] == ' ')
			textline->curs--;
		while (textline->curs > 0 && line[textline->curs - 1] != ' ')
			textline->curs--;
		edit_update_cursor();
	}
}

/* alt-B -> alt-K !! */
void
cx_edit_w_left(void)
{
	if (textline->curs > 0)
		cx_edit_w_left_();
	else
		win_remark("note: bookmark panel key is now Alt-K");
}

/* move one word right */
void
cx_edit_w_right(void)
{
	const char *line;

	if (textline->curs < textline->size) {
		line = USTR(textline->line);
		while (textline->curs < textline->size
		  && line[textline->curs] != ' ')
			textline->curs++;
		while (textline->curs < textline->size
		  && line[textline->curs] == ' ')
			textline->curs++;
		edit_update_cursor();
	}
}

/*
 * _nu_ means "no update" version, the caller is responsible
 * for calling the update function edit_update().
 *
 * The main advantage is you can invoke several _nu_ functions
 * and then make the 'update' just once.
 *
 * Note: The edit_update() consists of edit_adjust() followed by
 * win_edit(). If the 'offset' is fine (e.g. after edit_nu_kill),
 * you can skip the edit_adjust().
 */
void
edit_nu_kill(void)
{
	if (textline == &line_cmd)
		hist_reset_index();

	textline->curs = textline->size = textline->offset = 0;
	/*
	 * we call us_copy to possibly shrink the allocated memory block,
	 * other delete functions don't do that
	 */
	us_copy(&textline->line,"");
}

void
cx_edit_kill(void)
{
	edit_nu_kill();
	win_edit();
}

/* delete 'cnt' chars at cursor position */
static void
delete_chars(int cnt)
{
	int i;
	char *line;

	line = USTR(textline->line);
	textline->size -= cnt;
	for (i = textline->curs; i <= textline->size; i++)
		line[i] = line[i + cnt];
}

void
cx_edit_backsp(void)
{
	if (textline->curs) {
		textline->curs--;
		delete_chars(1);
		edit_update();
	}
}

void
cx_edit_delchar(void)
{
	if (textline->curs < textline->size) {
		delete_chars(1);
		edit_update();
	}
}

/* delete until the end of line */
void
cx_edit_delend(void)
{
	USTR(textline->line)[textline->size = textline->curs] = '\0';
	edit_update();
}

/* delete word */
static int
edit_w_del(void)
{
	int eow;
	char *line;

	eow = textline->curs;
	line = USTR(textline->line);
	if (line[eow] == ' ' || line[eow] == '\0')
		return 0;

	while (textline->curs > 0 && line[textline->curs - 1] != ' ')
		textline->curs--;
	while (eow < textline->size && line[eow] != ' ')
		eow++;
	while (line[eow] == ' ')
		eow++;
	delete_chars(eow - textline->curs);
	edit_update();

	return 1;
}

/* alt-D -> alt-W !! */
void
cx_edit_w_del(void)
{
	if (edit_w_del() == 0)
		win_remark("note: directory panel key is now Alt-W");
}
void
cx_edit_w_del_(void)
{
	edit_w_del();
}

/* make room for 'cnt' chars at cursor position */
static char *
insert_space(int cnt)
{
	int i;
	char *line, *ins;

	us_resize(&textline->line,textline->size + cnt + 1);
	line = USTR(textline->line);
	ins = line + textline->curs;	/* insert new character(s) here */
	textline->size += cnt;
	textline->curs += cnt;
	for (i = textline->size; i >= textline->curs; i--)
		line[i] = line[i - cnt];

	return ins;
}

void
edit_nu_insertchar(int ch)
{
	*insert_space(1) = (char)ch;
}

void
edit_insertchar(int ch)
{
	edit_nu_insertchar(ch);
	edit_update();
}

/* convert a decimal digit */
static int
conv_dec(int ch)
{
	if (ch >= '0' && ch <= '9')
		return ch - '0';
	return -1;
}

/* convert a hex digit */
static int
conv_hex(int ch)
{
	static char *hexnum = "0123456789ABCDEF0123456789abcdef";
	char *pch;

	if (IS_CHAR(ch) && (pch = strchr(hexnum,ch)))
		return (pch - hexnum) & 0x0F;
	return -1;
}

/* insert special character literally or using its ASCII code */
void cx_edit_insert_spc(void)
{
	int v1, v2, v3, val;
	int ch;

	ch = kbd_getraw();

	/* nnn decimal (i.e. type 064 for @) */
	if ((v1 = conv_dec(ch)) >= 0) {
		v2 = conv_dec(kbd_getraw());
		v3 = conv_dec(kbd_getraw());
		val = 100 * v1 + 10 * v2 + v3;
		if (v1 >= 0 && v2 >= 0 && v3 >= 0 && val > 0 && val < 256)
			edit_insertchar(val);
		else
			win_remark("usage: ctrl-V N N N, "
			  "where NNN is decimal ASCII code (001 to 255)");
		return;
	}

	/* Xnn hexadecimal (i.e. type X40 or x40 for @) */
	if (ch == 'x' || ch == 'X') {
		v1 = conv_hex(kbd_getraw());
		v2 = conv_hex(kbd_getraw());
		val = 16 * v1 + v2;
		if (v1 >= 0 && v2 >= 0 && val > 0)
			edit_insertchar(val);
		else
			win_remark("usage: ctrl-V x H H, "
			  "where HH is hex ASCII code (01 to FF)");
		return;
	}

	/* literal character */
	if (IS_CHAR(ch)) {
		if (ch != '\0')
			edit_insertchar(ch);
	}
	else
		win_remark("function key codes cannot be inserted");
}

void
cx_edit_paste_link(void)
{
	FILE_ENTRY *pfe;

	pfe = ppanel_file->files[ppanel_file->pd->curs];
	if (!pfe->symlink)
		win_remark("not a symbolic link");
	else {
		edit_nu_insertstr(USTR(pfe->link),1);
		edit_insertchar(' ');
	}
}

/* returns 1 if 'ch' is a shell metacharacter */
int
edit_isspecial(int ch)
{
	/* built-in metacharacters (let's play it safe) */
	if (strchr("\t ()<>[]{}#$&\\|?*;\'\"`~",ch))
		return 1;
	/* C-shell */
	if (clex_data.shelltype == 1 && (ch == '!' || ch == ':'))
		return 1;
	/* additional special characters to be quoted */
	if (*config_str(CFG_QUOTE) && strchr(config_str(CFG_QUOTE),ch))
		return 1;

	return 0;
}

/* set 'quoteflag' if special characters are to be quoted */
void
edit_nu_insertstr(const char *str, int quoteflag)
{
	char ch;
	int len, spec;
	char *ins;

	for (len = spec = 0; (ch = str[len]) != '\0'; len++)
		if (quoteflag && edit_isspecial((unsigned char)ch))
			spec++;

	if (len > 0) {
		ins = insert_space(len + spec);
		while ((ch = *str++) != '\0') {
			if (quoteflag && edit_isspecial((unsigned char)ch))
				*ins++ = '\\';
			*ins++ = ch;
		}
	}

	/*
	 * if 'quoteflag' is not set this is roughly equivalent to:
	 * len = strlen(str); strncpy(insert_space(len),src,len);
	 */
}

void
edit_insertstr(const char *str, int quoteflag)
{
	edit_nu_insertstr(str,quoteflag);
	edit_update();
}

void
edit_nu_putstr(const char *str)
{
	us_copy(&textline->line,str);
	textline->curs = textline->size = strlen(str);
}

void
edit_putstr(const char *str)
{
	edit_nu_putstr(str);
	edit_update();
}

/*
 * insert string, expand $x variables:
 *   $/ is ignored
 *   $$ -> literal $
 *   $1 -> current directory name (primary panel's directory)
 *   $2 -> secondary directory name (secondary panel's directory)
 *   $F -> current file name
 *   $S -> names of all selected file(s)
 *   $f -> $S - if the <ESC> key was pressed and
 *              at least one file has been selected
 *         $F - otherwise
 *   everything else is copied literally
 */
void
edit_macro(const char *macro)
{
	char ch, *ins;
	const char *src;
	int i, cnt, curs;
	FLAG prefix;
	FILE_ENTRY *pfe;

	/*
	 * implementation note: avoid char by char inserts whenever
	 * possible, inserting bigger chunks requires much less overhead
	 */

	if (textline->curs == 0 || USTR(textline->line)[textline->curs - 1] == ' ')
		while (*macro == ' ')
			macro++;

	curs = -1;
	for (src = macro, prefix = 0; (ch = *macro++) != '\0'; ) {
		if (TCLR(prefix)) {
			/* first insert everything we saw before the '$' prefix */
			cnt = macro - src - 2 /* two chars in "$x" */;
			if (cnt > 0)
				strncpy(insert_space(cnt),src,cnt);
			src = macro;

			/* now handle $x */
			if (ch == 'f' && panel->cnt > 0) {
				ch = ppanel_file->selected &&
				     (kbd_esc() || specialF) ? 'S' : 'F';
				if (ch == 'F' && ppanel_file->selected
				  && config_num(CFG_WARN_SELECT))
					win_remark("press <ESC> before <Fn> if you "
					  "want to work with selected files");
			}
			switch (ch) {
			case '$':
				edit_nu_insertchar('$');
				break;
			case '1':
				edit_nu_insertstr(USTR(ppanel_file->dir),1);
				break;
			case '2':
				edit_nu_insertstr(USTR(ppanel_file->other->dir),1);
				break;
			case 'c':
				curs = textline->curs;
				break;
			case 'S':
				if (panel->cnt > 0) {
					for (i = cnt = 0; cnt < ppanel_file->selected; i++) {
						pfe = ppanel_file->files[i];
						if (pfe->select) {
							if (cnt++)
								edit_nu_insertchar(' ');
							edit_nu_insertstr(SDSTR(pfe->file),1);
						}
					}
				}
				break;
			case 'F':
			case 'f':
				if (panel->cnt > 0) {
					pfe = ppanel_file->files[ppanel_file->pd->curs];
					edit_nu_insertstr(SDSTR(pfe->file),1);
				}
				break;
			case '/':
				edit_update();
				if (curs >= 0) textline->curs = curs;
				return;
			default:
				ins = insert_space(2);
				ins[0] = '$';
				ins[1] = ch;
			}
		}
		else if (ch == '$')
			prefix = 1;
	}

	/* insert the rest */
	edit_insertstr(src,0);

	if (curs >= 0)
		textline->curs = curs;
}

void cx_edit_cmd_f2(void)	{ edit_macro("$f "); 					}
void cx_edit_cmd_f3(void)	{ edit_macro(config_str(CFG_CMD_F3));	}
void cx_edit_cmd_f4(void)	{ edit_macro(config_str(CFG_CMD_F4));	}
void cx_edit_cmd_f5(void)	{ edit_macro(config_str(CFG_CMD_F5));	}
void cx_edit_cmd_f6(void)	{ edit_macro(config_str(CFG_CMD_F6));	}
void cx_edit_cmd_f7(void)	{ edit_macro(config_str(CFG_CMD_F7));	}
void cx_edit_cmd_f8(void)	{ edit_macro(config_str(CFG_CMD_F8));	}
void cx_edit_cmd_f9(void)	{ edit_macro(config_str(CFG_CMD_F9)); 	}
void cx_edit_cmd_f10(void)	{ edit_macro(config_str(CFG_CMD_F10));	}
void cx_edit_cmd_f11(void)	{ edit_macro(config_str(CFG_CMD_F11));	}
void cx_edit_cmd_f12(void)	{ edit_macro(config_str(CFG_CMD_F12));	}

void
cx_edit_paste_dir(void)
{
	edit_macro(kbd_esc() ? " $1" : " $2");
}

void
cx_edit_fullpath(void)
{
	edit_macro(strcmp(USTR(ppanel_file->dir),"/") ? " $1/$F " : " /$F ");
}

void
edit_setprompt(TEXTLINE *pline, const char *prompt)
{
	pline->promptlen = strlen(pline->prompt = prompt);
}

void
cx_insert_filename(void)
{
	edit_macro("$F ");
	next_mode = MODE_SPECIAL_RETURN;
}

void
cx_insert_filenames(void)
{
	if (ppanel_file->selected)
		edit_macro(" $S ");
	else
		win_remark("no selected files");
	next_mode = MODE_SPECIAL_RETURN;
}

void
cx_insert_fullpath(void)
{
	cx_edit_fullpath();
	next_mode = MODE_SPECIAL_RETURN;
}

void
cx_insert_d1(void)
{
	edit_macro(" $1");
	next_mode = MODE_SPECIAL_RETURN;
}

void
cx_insert_d2(void)
{
	edit_macro(" $2");
	next_mode = MODE_SPECIAL_RETURN;
}

void
cx_insert_link(void)
{
	cx_edit_paste_link();
	next_mode = MODE_SPECIAL_RETURN;
}
