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
#include <stdlib.h>			/* free() */
#include <string.h>			/* strcmp() */

#include "clex.h"
#include "history.h"

#include "cfg.h"			/* config_num() */
#include "edit.h"			/* edit_update() */
#include "inout.h"			/* win_remark() */
#include "panel.h"			/* pan_adjust() */
#include "util.h"			/* emalloc() */
#include "ustring.h"		/* us_copy() */

static HIST_ENTRY **history;/* command line history */
static int hs_alloc = 0;	/* number of allocated entries */
static int hs_cnt;			/* entries in use */
static int pn_index;		/* index for previous/next cmd */
static USTRING save_line = { 0,0 };
						/* for temporary saving of the command line */

void
hist_reconfig(void)
{
	int i;
	static HIST_ENTRY *storage;

	if (hs_alloc) {
		free(storage);
		free(history);
		free(panel_hist.hist);
	}

	hs_alloc = config_num(CFG_H_SIZE);
	storage = emalloc(hs_alloc * sizeof(HIST_ENTRY));
	history = emalloc(hs_alloc * sizeof(HIST_ENTRY *));
	panel_hist.hist = emalloc(hs_alloc * sizeof(HIST_ENTRY *));
	for (i = 0; i < hs_alloc; i++) {
		history[i] = storage + i;
		US_INIT(history[i]->cmd);
	}

	hs_cnt = 0;
	hist_reset_index();
}

void
hist_panel(void)
{
	int i, j;
	const char *filter;
	HIST_ENTRY *curs;

	if (VALID_CURSOR(panel_hist.pd))
		curs = panel_hist.hist[panel_hist.pd->curs];
	else {
		curs = 0;
		panel_hist.pd->curs = 0;
	}
	filter = panel_hist.pd->filtering ? panel_hist.pd->filter->line : 0;
	for (i = j = 0; i < hs_cnt; i++) {
		if (history[i] == curs)
			panel_hist.pd->curs = j;
		if (filter && !substring(USTR(history[i]->cmd),filter,0))
			continue;
		panel_hist.hist[j++] = history[i];
	}
	panel_hist.pd->cnt = j;
}

void
hist_prepare(void)
{
	panel_hist.pd->filtering = 0;
	hist_panel();
	panel_hist.pd->top = panel_hist.pd->min;
	panel_hist.pd->curs = pn_index > 0 ? pn_index : 0;

	panel = panel_hist.pd;
	textline = &line_cmd;
}

const HIST_ENTRY *
get_history_entry(int i)
{
	return i < hs_cnt ? history[i] : 0;
}

void
hist_reset_index(void)
{
	pn_index = -1;
}

/*
 * hist_save() puts the command 'cmd' on the top
 * of the command history list.
 */
void
hist_save(const char *cmd, int failed)
{
	int i;
	FLAG new;
	HIST_ENTRY *x, *top;

	hist_reset_index();

	new = 1;
	for (top = history[0], i = 0; i < hs_alloc; i++) {
		x = history[i];
		history[i] = top;
		top = x;
		if (i == hs_cnt) {
			hs_cnt++;
			break;
		}
		if (strcmp(USTR(top->cmd),cmd) == 0) {
			/* avoid duplicates */
			new = 0;
			break;
		}
	}
	if (new)
		us_copy(&top->cmd,cmd);
	top->failed = failed;
	
	history[0] = top;
}

/* file panel functions */

static void
warn_fail(int n)
{
	if (history[n]->failed)
		win_remark("this command failed last time");
}

/* copy next (i.e. more recent) command from the history list */
void
cx_hist_next(void)
{
	if (pn_index == -1) {
		win_remark("top of the history list");
		return;
	}

	if (pn_index-- == 0)
		edit_putstr(USTR(save_line));
	else {
		edit_putstr(USTR(history[pn_index]->cmd));
		warn_fail(pn_index);
	}
}

/* copy previous (i.e. older) command from the history list */
void
cx_hist_prev(void)
{
	if (pn_index >= hs_cnt - 1) {
		win_remark("bottom of the history list");
		return;
	}

	if (++pn_index == 0)
		us_xchg(&save_line,&line_cmd.line);
	edit_putstr(USTR(history[pn_index]->cmd));
	warn_fail(pn_index);
}

/* history panel functions */

void
cx_hist_paste(void)
{
	edit_insertstr(USTR(panel_hist.hist[panel_hist.pd->curs]->cmd),0);
}

void
cx_hist_enter(void)
{
	if (line_cmd.size == 0)
		cx_hist_paste();

	next_mode = MODE_SPECIAL_RETURN;
}

void
cx_hist_del(void)
{
	int i;
	HIST_ENTRY *del;
	FLAG move;

	del = panel_hist.hist[panel_hist.pd->curs];
	hs_cnt--;
	for (move = 0, i = 0; i < hs_cnt; i++) {
		if (history[i] == del) {
			if (pn_index > i)
				pn_index--;
			else if (pn_index == i)
				hist_reset_index();
			move = 1;
		}
		if (move)
			history[i] = history[i + 1];
	}
	history[hs_cnt] = del;
	hist_panel();
	pan_adjust(panel_hist.pd);
	win_panel();
}
