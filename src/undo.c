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

#include <sys/types.h>			/* clex.h */
#include <string.h>				/* strcmp() */

#include "clex.h"
#include "undo.h"

#include "control.h"			/* err_exit() */
#include "edit.h"				/* edit_update() */
#include "inout.h"				/* win_remark() */
#include "ustring.h"			/* USTRING */

static TEXTLINE *current;
static USTRING undo_line = { 0,0 };
static int undo_size, undo_curs, undo_offset;
static FLAG disable = 1;
		/* disable undo mechanism while performing undo operations */

EDIT_OP this_op;	/* description of the current operation */

/* initialize data structures */
void
undo_init(TEXTLINE *t)
{
	int i;

	for (i = 0; i < UNDO_LEVELS; i++)
		US_INIT(t->undo[i].save_line);
	t->last_op.code = OP_NONE;
	t->undo_base = t->undo_levels = t->redo_levels = 0;
}

/* clear the undo history */
void
undo_reset(void)
{
	if (textline == 0)
		return;

	textline->last_op.code = OP_NONE;
	textline->undo_levels = textline->redo_levels = 0;
	disable = 1;
}

/*
 * check if 'lstr' is the same as 'sstr' would be
 * with 'len' chars inserted at position 'pos'
 */
static int
cmp_strings(const char *sstr, const char *lstr, int pos, int len)
{
	return pos >= 0 && len >=0
	  && (pos == 0 || strncmp(sstr,lstr,pos) == 0)
	  && strcmp(sstr + pos,lstr + pos + len) == 0;
}

/* which edit operation was this one ? */
static void
tell_edit_op(void)
{
	int pos, len;
	const char *before, *after;

	before = USTR(undo_line);
	after = USTR(textline->line);
	len = textline->size - undo_size;
	if (len > 0) {
		if (cmp_strings(before,after,pos = textline->curs - len,len)) {
			this_op.code = OP_INS;
			this_op.pos = pos;
			this_op.len = len;
			return;
		}
	}
	else if (len == 0) {
		if (strcmp(before,after) == 0) {
			this_op.code = OP_NONE;
			return;
		}
	}
	else {
		if (cmp_strings(after,before,pos = textline->curs,len = -len)) {
			this_op.code = OP_DEL;
			this_op.pos = pos;
			this_op.len = len;
			return;
		}
	}

	this_op.code = OP_CHANGE;
	this_op.pos = this_op.len = 0;
}

/* make a copy of 'textline' before an edit operation ... */
void
undo_before(void)
{
	if (textline == 0)
		return;

	disable = 0;
	current = textline;
	us_copy(&undo_line,USTR(textline->line));
	undo_size   = textline->size;
	undo_curs   = textline->curs;
	undo_offset = textline->offset;
}

/* ... and now see what happened with it afterwards */
void
undo_after(void)
{
	FLAG merge;
	int idx;

	if (TSET(disable) || textline == 0 || textline != current)
		return;

	tell_edit_op();
	if (this_op.code == OP_NONE)
		return;

	/* can this edit operation be merged with the previous one ? */
	merge = (this_op.code == OP_INS && textline->last_op.code == OP_INS
	    && this_op.len + textline->last_op.len < 10
	    && this_op.pos == textline->last_op.pos + textline->last_op.len)
	  || (this_op.code == OP_DEL && textline->last_op.code == OP_DEL
	    && this_op.len == 1 && textline->last_op.len == 1
	    && (this_op.pos == textline->last_op.pos
	      || this_op.pos == textline->last_op.pos - 1));

	textline->last_op = this_op;	/* struct copy */
	textline->redo_levels = 0;

	if (merge)
		return;

	idx = (textline->undo_base + textline->undo_levels) % UNDO_LEVELS;
	if (textline->undo_levels < UNDO_LEVELS)
		textline->undo_levels++;
	else
		textline->undo_base = (textline->undo_base + 1) % UNDO_LEVELS;

	us_xchg(&textline->undo[idx].save_line,&undo_line);
	textline->undo[idx].save_size   = undo_size;
	textline->undo[idx].save_curs   = undo_curs;
	textline->undo[idx].save_offset = undo_offset;
}

static void
undo_redo(int op)
{
	int idx;

	if (textline == 0)
		return;

	if (op) {
		if (textline->undo_levels == 0) {
			win_remark("undo not possible");
			return;
		}
		idx = --textline->undo_levels;
		textline->redo_levels++;
	}
	else {
		if (textline->redo_levels == 0) {
			win_remark("redo not possible");
			return;
		}
		idx = textline->undo_levels++;
		textline->redo_levels--;
	}
	idx = (textline->undo_base + idx) % UNDO_LEVELS;

	us_xchg(&textline->line,&textline->undo[idx].save_line);
	textline->size   = textline->undo[idx].save_size;
	textline->curs   = textline->undo[idx].save_curs;
	textline->offset = textline->undo[idx].save_offset;

	us_xchg(&textline->undo[idx].save_line,&undo_line);
	textline->undo[idx].save_size   = undo_size;
	textline->undo[idx].save_curs   = undo_curs;
	textline->undo[idx].save_offset = undo_offset;

	edit_update();
	textline->last_op.code = OP_CHANGE;
	disable = 1;
}

void
cx_undo(void)
{
	undo_redo(1);
}

void
cx_redo(void)
{
	undo_redo(0);
}
