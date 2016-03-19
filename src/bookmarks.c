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

#include <sys/types.h>		/* clex.h */
#include <sys/stat.h>		/* umask() */
#include <errno.h>			/* errno */
#include <stdarg.h>			/* va_list */
#include <stdio.h>			/* vprint() */
#include <stdlib.h>			/* free() */
#include <string.h>			/* strerror() */

#include "clex.h"
#include "bookmarks.h"

#include "completion.h"		/* compl_file() */
#include "control.h"		/* control_loop() */
#include "edit.h"			/* edit_setprompt() */
#include "filepanel.h"		/* changedir() */
#include "inout.h"			/* txt_printf() */
#include "panel.h"			/* pan_adjust() */
#include "ustring.h"		/* us_reset() */
#include "util.h"			/* read_file() */

/* limits to protect resources */
#define BM_FILESIZE_LIMIT	10000
#define BM_ENTRIES_LIMIT	200

static USTRING bm_list[BM_ENTRIES_LIMIT];	/* list of bookmarks */
static int bm_cnt = 0;				/* number of bookmarks */
static FLAG changed = 0;			/* bookmarks have changed */
static const char *user_bm_file;	/* personal bookmarks filename */
static time_t bm_file_mod = 0;		/* last modification of the file */

extern int errno;

static void
bm_error(const char *format, ...)
{
	va_list argptr;

	txt_printf("BOOKMARKS: Reading the bookmark file \"%s\"\n",
	  user_bm_file);

	va_start(argptr,format);
	fputs("BOOKMARKS: ",stdout);
	vprintf(format,argptr);
	va_end(argptr);
}

/*
 * return value:
 *  -1 = failure, original bookmarks are unchanged
 *   0 = bookmarks loaded (with or without problems)
 */
static int
read_bm_file(void)
{
	int  error;
	size_t filesize;
	char *source, *ptr;

	filesize = BM_FILESIZE_LIMIT;
	source = read_file(user_bm_file,&filesize,&error);
	if (source == 0) {
		switch (error) {
		case 1:
			if (errno == ENOENT) {
				bm_cnt = 0;
				return 0;	/* no file is OK */
			}
			bm_error("Cannot open the file (%s)\n",strerror(errno));
			break;
		case 2:
			bm_error("File is too big, limit is "
			  STR(BM_FILESIZE_LIMIT) " bytes\n");
			break;
		case 3:
			bm_error("File read error (%s)\n",strerror(errno));
		}
		return -1;
	}

	bm_cnt = 0;
	for (ptr = source; ptr < source + filesize; ptr++)
		if (*ptr == '\n')
			*ptr = '\0';

	for (ptr = source; ptr < source + filesize; ) {
		if (*ptr == '/') {
			if (bm_cnt == BM_ENTRIES_LIMIT) {
				bm_error("Too many lines, limit is "
				  STR(BM_ENTRIES_LIMIT) "\n");
				break;
			}
			us_copy(&bm_list[bm_cnt],ptr);
			bm_cnt++;
		}
		/* advance to the next line */
		while (*ptr++)
			;
	}
	free(source);
	return 0;
}

void
bm_initialize(void)
{
	int i;
	time_t mod;

	panel_bm_lst.bm = panel_bm_mng.bm = bm_list;
	for (i = 0; i < BM_ENTRIES_LIMIT; i++)
		US_INIT(bm_list[i]);

	pathname_set_directory(clex_data.homedir);
	user_bm_file = estrdup(pathname_join(".clexbm"));

	mod = mod_time(user_bm_file);
	if (read_bm_file() == 0)
		bm_file_mod = mod;
}

void
bm_list_prepare(void)
{
	time_t mod;

	mod = mod_time(user_bm_file);
	if (bm_file_mod != mod && read_bm_file() == 0) {
		bm_file_mod = mod;
		win_warning("Latest version of the bookmarks was loaded from the file.");
	}

	panel_bm_lst.pd->cnt = bm_cnt;
	if (panel_bm_lst.pd->curs < 0)
		panel_bm_lst.pd->curs = panel_bm_lst.pd->top =  panel_bm_lst.pd->min;
	/* otherwise leave the cursor where it was */

	panel = panel_bm_lst.pd;
	textline = 0;
}

void
bm_mng_prepare(void)
{
	panel_bm_mng.pd->cnt = bm_cnt;
	panel_bm_mng.pd->top = panel_bm_mng.pd->curs = panel_bm_mng.pd->min;

	panel = panel_bm_mng.pd;
	textline = 0;
}

void
bm_edit_prepare(void)
{
	/* panel = panel_bm_mng.pd; */
	textline = &line_tmp;
	edit_setprompt(textline,"bookmark: ");
	edit_nu_putstr(USTR(bm_list[panel_bm_mng.pd->curs]));
}

static void
bm_save(void)
{
	int i;
	FLAG errflag;
	FILE *fp;

	umask(clex_data.umask | 022);
	fp = fopen(user_bm_file,"w");
	umask(clex_data.umask);

	if (fp == 0) {
		win_warning("BOOKMARKS: Cannot open the bookmark file "
		  "for writing.");
		return;
	}
	fprintf(fp,	"#\n"
				"# CLEX bookmark file\n"
				"#\n");
	for (i = 0; i < bm_cnt; i++)
		fprintf(fp,"%s\n",USTR(bm_list[i]));
	errflag = ferror(fp) != 0;
	if (fclose(fp) || errflag)
		win_warning("BOOKMARKS: File write error occurred.");
	else {
		win_remark("bookmark file updated");
		changed = 0;
	}
	bm_file_mod = mod_time(user_bm_file);
}

void
cx_bm_list_bookmark(void)
{
	const char *dir;
	int i;

	dir = USTR(ppanel_file->dir);
	for (i = 0; i < bm_cnt; i++)
		if (strcmp(USTR(bm_list[i]),dir) == 0) {
			win_remark("already bookmarked");
			panel_bm_lst.pd->curs = i;
			pan_adjust(panel_bm_lst.pd);
			win_panel();
			return;
		}

	if (bm_cnt == BM_ENTRIES_LIMIT) {
		win_warning("Bookmark list is full.");
		return;
	}

	us_copy(&bm_list[bm_cnt],dir);
	panel_bm_lst.pd->curs = bm_cnt;
	panel_bm_lst.pd->cnt = ++bm_cnt;
	pan_adjust(panel_bm_lst.pd);
	win_panel();

	changed = 1;
	bm_save();
}

void
cx_bm_list_enter(void)
{
	if (changedir(USTR(bm_list[panel_bm_lst.pd->curs])) == 0)
		next_mode = MODE_SPECIAL_RETURN;
}

void
cx_bm_mng_revert(void)
{
	time_t mod;

	if (!changed) {
		win_remark("no changes were made");
		return;
	}

	bm_cnt = 0;
	mod = mod_time(user_bm_file);
	if (read_bm_file() == 0) {
		bm_file_mod = mod;
		changed = 0;
		win_remark("bookmarks reverted to previous state");
	}
	panel_bm_mng.pd->cnt = bm_cnt;
	win_panel();
}

void
cx_bm_mng_save(void)
{
	if (changed)
		bm_save();
	if (next_mode == 0)
		next_mode = MODE_SPECIAL_RETURN;
	/* otherwise next_mode is already set by EXTRA_LINE table */
}

void
cx_bm_mng_edit(void)
{
	control_loop(MODE_BM_EDIT);
	win_panel_opt();
}

void
cx_bm_edit_enter(void)
{
	if (*USTR(textline->line) != '/') {
		win_warning("Directory name must start with a slash / . ");
		return;
	}
	us_xchg(&textline->line,&bm_list[panel_bm_mng.pd->curs]);
	changed = 1;
	next_mode = MODE_SPECIAL_RETURN;
}

void
cx_bm_edit_compl(void)
{
	if (compl_file(COMPL_TYPE_DIRPANEL) < 0)
		win_remark("COMPLETION: please type at least the "
		  "first character");
}

void
cx_bm_mng_up(void)
{
	int pos;

	if ((pos = panel_bm_mng.pd->curs) == 0)
		return;

	us_xchg(&bm_list[pos],&bm_list[panel_bm_mng.pd->curs = pos - 1]);
	changed = 1;

	win_panel();
}

void
cx_bm_mng_down(void)
{
	int pos;

	if ((pos = panel_bm_mng.pd->curs) == bm_cnt - 1)
		return;

	us_xchg(&bm_list[pos],&bm_list[panel_bm_mng.pd->curs = pos + 1]);
	changed = 1;

	win_panel();
}

void
cx_bm_mng_del(void)
{
	int i, del;
	USTRING x;

	del = panel_bm_mng.pd->curs;

	panel_bm_mng.pd->cnt = --bm_cnt;
	if (panel_bm_mng.pd->curs == bm_cnt) {
		panel_bm_mng.pd->curs--;
		pan_adjust(panel_bm_mng.pd);
	}

	/* these are struct copy operations */
	x = bm_list[del];
	for (i = del; i < bm_cnt; i++)
		bm_list[i] = bm_list[i + 1];
	bm_list[bm_cnt] = x;
	changed = 1;

	win_panel();
}

void
cx_bm_mng_new(void)
{
	int i, ins;
	USTRING x;

	if (bm_cnt == BM_ENTRIES_LIMIT) {
		win_warning("Bookmark list is full.");
		return;
	}

	LIMIT_MIN(panel_bm_mng.pd->curs,-1);
	ins = ++panel_bm_mng.pd->curs;

	/* these are struct copy operations */
	x = bm_list[bm_cnt];
	for (i = bm_cnt; i > ins; i--)
		bm_list[i] = bm_list[i - 1];
	bm_list[ins] = x;
	panel_bm_mng.pd->cnt = ++bm_cnt;
	us_copy(&bm_list[ins],"-- new bookmark --");
	changed = 1;

	pan_adjust(panel_bm_mng.pd);
	win_panel();
	us_copy(&bm_list[ins],"/");
	cx_bm_mng_edit();
}
