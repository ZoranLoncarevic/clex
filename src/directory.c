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
#include <stdlib.h>			/* qsort() */
#include <string.h>			/* strcmp() */

#include "clex.h"
#include "directory.h"

#include "cfg.h"			/* config_num() */
#include "completion.h"		/* compl_file() */
#include "control.h"		/* get_current_mode() */
#include "edit.h"			/* edit_setprompt() */
#include "filepanel.h"		/* changedir() */
#include "inout.h"			/* win_panel() */
#include "panel.h"			/* pan_adjust() */
#include "sdstring.h"		/* sd_reset() */
#include "undo.h"			/* undo_init() */
#include "ustring.h"		/* us_copy() */
#include "userdata.h"		/* dir_tilde() */
#include "util.h"			/* emalloc() */

/*
 * The directory list 'dirlist' maintained here contains:
 *   a) the names of recently visited directories, it is
 *      a source for the directory panel 'panel_dir'
 *   b) the last cursor position in all those directories, allowing
 *      to restore the cursor position when the user re-enters
 *      an already visited directory
 *
 * the 'dirlist' is never empty, the first element is inserted
 * already during startup
 */

/* additional entries to be allocated when the 'dirlist' table is full */
#define SAVEDIR_ALLOC_UNIT	32
#define SAVEDIR_ALLOC_MAX	384	/* 'dirlist' size limit */

typedef struct {
	USTRING dirname;		/* directory name */
	SDSTRING savefile;		/* file panel's current file */
	int savetop, savecurs;	/* top line, cursor line */
} SAVEDIR;

static SAVEDIR **dirlist;	/* list of visited directories */
static int diralloc = 0;	/* number of allocated entries in 'dirlist' */
static int dircnt = 0;		/* number of used entries in 'dirlist' */

/* directory panel's data is built from 'dirlist' */
#define DP_LIST (panel_dir.dir)
static int dp_alloc = 0;	/* max number of entries in the dir panel */
static int dp_max;			/* max number of entries to be used */

/* split directory panel */
#define DPS_LIST (panel_dir_split.dir)
static int dps_alloc = 0;	/* max number of entries in the dir_split panel */

void
dir_initialize(void)
{
	US_INIT(line_dir.line);
	undo_init(&line_dir);
	edit_setprompt(&line_dir,"Change directory: ");

	dir_reconfig();
}

void
dir_reconfig(void)
{
	if (dp_alloc)
		free(DP_LIST);

	dp_max = dp_alloc = config_num(CFG_D_SIZE);
	if (dp_max == 0)	/* D_PANEL_SIZE = AUTO */
		dp_alloc = 100;	/* enough for every screen */
	DP_LIST = emalloc(dp_alloc * sizeof(DIR_ENTRY));
}

/*
 * length of the common part of two directory names (FQDNs)
 * (this is not the same as common part of two strings)
 */
static size_t
common_part(const char *dir1, const char *dir2)
{
	char ch1, ch2;
	size_t i, slash;

	for (i = slash = 0; /* until return */; i++) {
		ch1 = dir1[i];
		ch2 = dir2[i];
		if (ch1 == '\0')
			return ch2 == '/' || ch2 == '\0' ? i : slash;
		if (ch2 == '\0')
			return ch1 == '/' ? i : slash;
		if (ch1 != ch2)
			return slash;
		if (ch1 == '/')
			slash = i;
	}
}

/*
 * check the relationship of two directories (FQDNs)
 *   return -1: dir2 is subdir of dir1 (/dir2 = /dir1 + /sub)
 *   return +1: dir1 is subdir of dir2 (/dir1 = /dir2 + /sub)
 *   return  0: otherwise
 */
static int
check_subdir(const char *dir1, const char *dir2)
{
	size_t slash;

	slash = common_part(dir1,dir2);
	return (dir2[slash] == '\0') - (dir1[slash] == '\0');
}

/*
 * comparison function for qsort() - comparing directories
 * (this is not the same as comparing strings)
 */
static int
qcmp(const void *e1, const void *e2)
{
	static USTRING dup1 = { 0,0 }, dup2 = { 0,0 };
	size_t slash, len1, len2;
	const char *dir1, *dir2;

	dir1 = ((DIR_ENTRY *)e1)->name;
	dir2 = ((DIR_ENTRY *)e2)->name;

	/* skip the common part */
	slash = common_part(dir1,dir2);
	if (dir1[slash] == '\0' && dir2[slash] == '\0')
		return 0;
	if (dir1[slash] == '\0')
		return -1;
	if (dir2[slash] == '\0')
		return 1;

	/* compare one directory component */
	slash++;
	for (len1 = slash; dir1[len1] != '/' && dir1[len1] != '\0'; len1++)
		;
	us_copyn(&dup1,dir1 + slash,len1);
	for (len2 = slash; dir2[len2] != '/' && dir2[len2] != '\0'; len2++)
		;
	us_copyn(&dup2,dir2 + slash,len2);

	return (config_num(CFG_COLLATION) ? STRCOLL : strcmp)
	  (USTR(dup1),USTR(dup2));
}

/*
 * In order not to waste CPU cycles, the panel 'panel_dir'
 * is not maintained continuously. Following function builds the
 * directory panel from the 'dirlist'.
 */
#define NO_COMPACT	5	/* preserve top 5 directory names */
void
dir_main_panel(void)
{
	int i, j, cnt, sub;
	FLAG store;
	const char *dirname, *filter;

	/* D_PANEL_SIZE = AUTO */
	if (config_num(CFG_D_SIZE) == 0) {
		/*
		 * substract extra lines and leave the bottom
		 * line empty to show there is no need to scroll
		 */
		dp_max = display.panlines + panel_dir.pd->min - 1;
		LIMIT_MAX(dp_max,dp_alloc);
	}

	filter = panel_dir.pd->filtering ? panel_dir.pd->filter->line : 0;

	for (i = cnt = 0; i < dircnt; i++) {
		if (cnt == dp_max)
			break;
		dirname = USTR(dirlist[i]->dirname);
		if (filter && !substring(dirname,filter,0))
			continue;
		/* compacting */
		store = 1;
		if (i >= NO_COMPACT)
			for (j = 0; j < cnt; j++) {
				sub = check_subdir(dirname,DP_LIST[j].name);
				if (sub == -1)
					store = 0;
				else if (sub == 1 && j >= NO_COMPACT) {
					DP_LIST[j].name = dirname;
					store = 0;
				}
		}
		if (store)
			DP_LIST[cnt++].name = dirname;
	}

	qsort(DP_LIST,cnt,sizeof(DIR_ENTRY),qcmp);

	/*
	 * Two lines like these:
	 *     /aaa/bbb/111
	 *     /aaa/bbb/2222
	 * are displayed as:
	 *     /aaa/bbb/111
	 *           __/2222
	 * and for that purpose a string length 'shlen' is computed
	 *     |<---->|
	 */
	DP_LIST[0].shlen = 0;
	for (i = 1; i < cnt; i++)
		DP_LIST[i].shlen =
			common_part(DP_LIST[i].name,DP_LIST[i - 1].name);

	panel_dir.pd->cnt = cnt;
}

void
dir_main_prepare(void)
{
	int i;
	const char *prevdir;

	panel_dir.pd->filtering = 0;
	dir_main_panel();
	panel_dir.pd->norev = 0;
	panel_dir.pd->top = panel_dir.pd->min;
	/* set cursor to previously used directory */
	panel_dir.pd->curs = 0;
	prevdir = USTR(dirlist[(dircnt >= 2)]->dirname);	/* [1] or [0] */
	for (i = 0; i < panel_dir.pd->cnt; i++)
		if (DP_LIST[i].name == prevdir) {
			panel_dir.pd->curs = i;
			break;
		}
	panel = panel_dir.pd;
	textline = &line_dir;
	edit_nu_kill();
}

static const char *
sub_dir(int pos)
{
	static USTRING tmp = { 0,0 };

	us_copyn(&tmp,DPS_LIST[pos].name,DPS_LIST[pos].shlen);
	return USTR(tmp);
}

void
dir_split_prepare(void)
{
	char ch;
	int cnt, i;
	const char *dirname;

	dirname = DP_LIST[panel_dir.pd->curs].name;
	cnt = 1;
	if (dirname[1]) {
		for (i = 0; (ch = dirname[i]); i++ )
			if (ch == '/')
				cnt++;
	}
	else
		i = 1;
	/* i is now strlen(dirname) */

	if (cnt > dps_alloc) {
		if (dps_alloc)
			free(DPS_LIST);
		dps_alloc = cnt;
		DPS_LIST = emalloc(dps_alloc * sizeof(DIR_ENTRY));
	}

	DPS_LIST[0].name = dirname;
	DPS_LIST[0].shlen = i;
	cnt = 1;
	if (dirname[1]) {
		while (--i >= 0)
			if (dirname[i] == '/') {
				DPS_LIST[cnt].name = dirname;
				DPS_LIST[cnt].shlen = i ? i : 1;	/* 0 -> 1 */
				cnt++;
			}
	}

	panel_dir_split.pd->cnt = cnt;
	panel_dir_split.pd->top = panel_dir_split.pd->min;
	panel_dir_split.pd->curs = 0;
	panel_dir_split.pd->norev = 0;

	panel = panel_dir_split.pd;

	/* textline inherited */
}

/*
 * save the current directory name and the current cursor position
 * in the file panel to 'dirlist'
 */
void
filepos_save(void)
{
	int i;
	FLAG new;
	SAVEDIR *x, *top;

	if (dircnt == diralloc && diralloc < SAVEDIR_ALLOC_MAX) {
		diralloc += SAVEDIR_ALLOC_UNIT;
		dirlist = erealloc(dirlist,diralloc * sizeof(SAVEDIR *));
		x = emalloc(SAVEDIR_ALLOC_UNIT * sizeof(SAVEDIR));
		for (i = 0; i < SAVEDIR_ALLOC_UNIT; i++) {
			US_INIT(x[i].dirname);
			SD_INIT(x[i].savefile);
			dirlist[dircnt + i] = x + i;
		}
	}

	for (new = 1, top = dirlist[0], i = 0; i <= dircnt && i < SAVEDIR_ALLOC_MAX; i++) {
		x = dirlist[i];
		dirlist[i] = top;
		top = x;
		if (i == dircnt) {
			dircnt++;
			break;
		}
		if (strcmp(USTR(top->dirname),USTR(ppanel_file->dir)) == 0) {
			/* no duplicates allowed */
			new = 0;
			break;
		}
	}
	if (new)
		us_copy(&top->dirname,USTR(ppanel_file->dir));
	if (ppanel_file->pd->cnt) {
		sd_copy(&top->savefile,
		  SDSTR(ppanel_file->files[ppanel_file->pd->curs]->file));
		top->savecurs = ppanel_file->pd->curs;
		top->savetop = ppanel_file->pd->top;
	}
	else if (new) {
		sd_copy(&top->savefile,"..");
		top->savecurs = 0;
		top->savetop = 0;
	}
	dirlist[0] = top;
}

/* set the file panel cursor according to data stored in 'dirlist' */
void
filepos_set(void)
{
	char *dir;
	int i, line;
	size_t len;
	SAVEDIR *pe;

	if (ppanel_file->pd->cnt) {
		dir = USTR(ppanel_file->dir);
		len = strlen(dir);
		for (i = 0; i < dircnt; i++) {
			pe = dirlist[i];
			if (strcmp(dir,USTR(pe->dirname)) == 0) {
				/* found */
				line = files_find(SDSTR(pe->savefile));
				ppanel_file->pd->curs = line >= 0 ? line : pe->savecurs;
				ppanel_file->pd->top = pe->savetop;
				pan_adjust(ppanel_file->pd);
				return;
			}
		}

		/* not found */
		line = files_find("..");
		ppanel_file->pd->curs = line >= 0 ? line : 0;
		ppanel_file->pd->top = ppanel_file->pd->min;
	}
	pan_adjust(ppanel_file->pd);
}

/* following cx_dir_xxx functions are used in both MODE_DIR_XXX modes */

static const char *
selected_dir(void)
{
	return get_current_mode() == MODE_DIR_SPLIT ?
	  sub_dir(panel_dir_split.pd->curs) : DP_LIST[panel_dir.pd->curs].name;
}

void
cx_dir_tab(void)
{
	const char *dir;

	if (textline->size)
		compl_file(COMPL_TYPE_DIRPANEL);
	else if (panel->curs >= 0) {
		dir = selected_dir();
		edit_nu_insertstr(dir,0);
		if (dir[1])
			edit_nu_insertchar('/');
		edit_update();
	}
}

void
cx_dir_enter(void)
{
	const char *dir;

	if (panel->norev) {
		/* focus on the input line */
		if (textline->size == 0)
			return;
		dir = dir_tilde(USTR(textline->line));
	}
	else {
		/* focus on the panel */
		if (panel->curs < 0)
			/* next_mode is set by the EXTRA_LINE table */
			return;
		if (get_current_mode() == MODE_DIR_SPLIT)
			dir = sub_dir(panel_dir_split.pd->curs);
		else {
			/* DIR_MAIN -> DIR_SPLIT */
			cx_edit_kill();
			next_mode = MODE_DIR_SPLIT;
			return;
		}
	}

	if (changedir(dir) == 0) {
		next_mode = MODE_SPECIAL_RETURN;
		return;
	}

	if (dir[0] == ' ' || dir[strlen(dir) - 1] == ' ')
		/* common user error */
		win_remark("check the spaces around the directory name");
	next_mode = 0;
}
