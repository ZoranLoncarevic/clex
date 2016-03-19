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

#include <sys/types.h>		/* clex.h */
#include <stdlib.h>			/* free() */

#include "clex.h"
#include "filter.h"

#include "directory.h"		/* dir_main_panel() */
#include "history.h"		/* hist_panel() */
#include "inout.h"			/* win_edit() */
#include "match.h" 			/* match() */
#include "panel.h" 			/* pan_adjust() */
#include "sdstring.h"		/* SDSTR() */
#include "sort.h"			/* sort_files() */
#include "util.h"			/* substring() */
#include "userdata.h"		/* user_panel() */

void
cx_filteredit_begin(void)
{
	panel->filter->curs = 0;
}

void
cx_filteredit_end(void)
{
	panel->filter->curs = panel->filter->size;
}

void
cx_filteredit_left(void)
{
	if (panel->filter->curs > 0)
		panel->filter->curs--;
}

void
cx_filteredit_right(void)
{
	if (panel->filter->curs < panel->filter->size)
		panel->filter->curs++;
}

void
cx_filteredit_kill(void)
{
	panel->filter->line[panel->filter->curs = panel->filter->size = 0] = '\0';
	panel->filter->changed = 1;
	win_filter();
}

/* delete 'cnt' chars at cursor position */
static void
delete_chars(int cnt)
{
	int i;

	panel->filter->size -= cnt;
	for (i = panel->filter->curs; i <= panel->filter->size; i++)
		panel->filter->line[i] = panel->filter->line[i + cnt];
	panel->filter->changed = 1;
}

void
cx_filteredit_backsp(void)
{
	if (panel->filter->curs) {
		panel->filter->curs--;
		delete_chars(1);
		win_filter();
	}
}

void
cx_filteredit_delchar(void)
{
	if (panel->filter->curs < panel->filter->size) {
		delete_chars(1);
		win_filter();
	}
}

/* delete until the end of line */
void
cx_filteredit_delend(void)
{
	panel->filter->line[panel->filter->size = panel->filter->curs] = '\0';
	panel->filter->changed = 1;
	win_filter();
}

/* make room for 'cnt' chars at cursor position */
static char *
insert_space(int cnt)
{
	int i;
	char *ins;

	if (panel->filter->size + cnt >= INPUT_STR)
		return 0;

	ins = panel->filter->line + panel->filter->curs;	/* insert new character(s) here */
	panel->filter->size += cnt;
	panel->filter->curs += cnt;
	for (i = panel->filter->size; i >= panel->filter->curs; i--)
		panel->filter->line[i] = panel->filter->line[i - cnt];

	panel->filter->changed = 1;
	return ins;
}

void
filteredit_insertchar(int ch)
{
	char *ins;

	if ( (ins = insert_space(1)) ) {
		*ins = (char)ch;
		win_filter();
	}
}

/* * * filter_update functions * * */

static int
ftype(const char *expr)
{
	char ch;

	while ( (ch = *expr++) ) {
		if (ch == '*' || ch == '?' || ch == '[')
			return 1;
	}
	return 0;
}

static void
filter_update_file(void)
{
	const char *filter;
	int i, j, cnt, type;
	FILE_ENTRY *pfe;

	if (ppanel_file->filt_cnt == 0)
		return;

	filter = ppanel_file->pd->filter->line;
	type = ftype(filter);	/* 1 pattern, 0 substring */
	if (ppanel_file->filtype != type) {
		ppanel_file->filtype = type;
		win_filter();
	}
	if (type && check_sre(filter) != 0) {
		win_remark("pattern is incomplete");
		return;
	}

	/* set fmatch flag */
	ppanel_file->selected = ppanel_file->filt_sel = 0;
	for (i = cnt = 0; i < ppanel_file->filt_cnt; i++) {
		pfe = ppanel_file->files[i];
		pfe->fmatch = type ? (*filter == '\0' || match(SDSTR(pfe->file)))
		  : substring(SDSTR(pfe->file),filter,0);
		if (pfe->fmatch) {
			cnt++;
			if (pfe->select)
				 ppanel_file->selected++;
		}
		else if (pfe->select)
			 ppanel_file->filt_sel++;
	}

	/* sort: fmatch entries before !fmatch entries */
	filepos_save();
	for (i = 0, j = ppanel_file->filt_cnt; i < cnt; i++) {
		if (!ppanel_file->files[i]->fmatch) {
			while (!ppanel_file->files[--j]->fmatch)
				;
			pfe = ppanel_file->files[i];
			ppanel_file->files[i] = ppanel_file->files[j];
			ppanel_file->files[j] = pfe;
		}
	}

	/* present only the fmatch entries */
	ppanel_file->pd->cnt = cnt;
	sort_files();
	filepos_set();
}

static void
filter_update_dir(void)
{
	int i, save_curs_rel;
	const char *save_curs_dir;

	/* save cursor */
	if (panel_dir.pd->cnt) {
		save_curs_dir = panel_dir.dir[panel_dir.pd->curs].name;
		save_curs_rel = (100 * panel_dir.pd->curs) / panel_dir.pd->cnt;	/* % */
	}
	else {
		save_curs_dir = 0;
		save_curs_rel = 0;
	}

	/* apply filter */
	dir_main_panel();

	/* restore cursor */
	if (save_curs_dir)
		for (i = 0; i < panel_dir.pd->cnt; i++)
			if (panel_dir.dir[i].name == save_curs_dir) {
				panel_dir.pd->curs = i;
				return;
			}
	panel_dir.pd->curs = (save_curs_rel * panel_dir.pd->cnt) / 100;
}

void
filter_update(void)
{
	switch (panel->type) {
	case PANEL_TYPE_DIR:
		filter_update_dir();
		break;
	case PANEL_TYPE_FILE:
		filter_update_file();
		break;
	case PANEL_TYPE_GROUP:
		group_panel();
		break;
	case PANEL_TYPE_HIST:
		hist_panel();
		break;
	case PANEL_TYPE_USER:
		user_panel();
		break;
	}
	panel->filter->changed = 0;
	pan_adjust(panel);
	win_panel();
}

/* * * filter_on, filter_off functions * * */

/*
 * note that filepanel filter can be controlled also from the main menu
 * and in such case 'panel' does not refer to the file panel; for this
 * reason all file panel operations which are available from the main menu
 * must use 'ppanel_file' instead of 'panel'
 */

static void
filter_off_file(void)
{
	ppanel_file->pd->filtering = 0;
	ppanel_file->pd->cnt = ppanel_file->filt_cnt;
	ppanel_file->selected += ppanel_file->filt_sel;
	filepos_save();
	sort_files();
	filepos_set();
}

void
filter_off(void)
{
	if (panel->type == PANEL_TYPE_FILE)
		filter_off_file();
	else {
		/* PANEL_TYPE_DIR, PANEL_TYPE_GROUP, PANEL_TYPE_HIST, PANEL_TYPE_USER */
		panel->filtering = 0;
		filter_update();
		win_filter();
	}

	pan_adjust(panel);
	win_filter();
	win_panel();
}

static void
filter_on_file(void)
{
	ppanel_file->pd->filtering = 1;
	ppanel_file->pd->filter->curs = ppanel_file->pd->filter->size = 0;
	ppanel_file->pd->filter->line[0] = '\0';
	ppanel_file->pd->filter->changed = 0;
	ppanel_file->filtype = 0;
	ppanel_file->filt_cnt = ppanel_file->pd->cnt;
	ppanel_file->filt_sel = 0;
}

void
cx_filter(void)
{
	if (panel->filter == 0) {
		win_remark("this panel does not support filtering");
		return;
	}

	if (panel->filtering == 0) {
		if (panel->type == PANEL_TYPE_FILE) {
			filter_on_file();
			win_filter();
		} else {
			panel->filtering = 1;
			cx_filteredit_kill();
			panel->filter->changed = 0;
		}
	}
	else if (panel->filter->size == 0)
		filter_off();
	else
		/* switch focus: filter line (1) <--> input line (2) */
		panel->filtering = 3 - panel->filtering;
}

void
cx_filter_toggle(void)
{
	if (ppanel_file->pd->filtering)
		filter_off_file();
	else
		filter_on_file();
}
