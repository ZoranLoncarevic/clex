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

#include "clex.h"
#include "panel.h"

#include "inout.h"		/* win_panel_opt() */

void
cx_pan_up(void)
{
	if (panel->curs > panel->min) {
		panel->curs--;
		LIMIT_MAX(panel->top,panel->curs);
		win_panel_opt();
	}
}

void
cx_pan_down(void)
{
	if (panel->curs < panel->cnt - 1) {
		panel->curs++;
		LIMIT_MIN(panel->top,panel->curs - display.panlines + 1);
		win_panel_opt();
	}
}

void
cx_pan_home(void)
{
	panel->top = panel->curs = panel->min;
	win_panel_opt();
}

void
cx_pan_end(void)
{
	panel->curs = panel->cnt - 1;
	LIMIT_MIN(panel->top,panel->curs - display.panlines + 1);
	win_panel_opt();
}

void
cx_pan_pgup(void)
{
	if (panel->curs > panel->min) {
		if (panel->curs > panel->top)
			panel->curs = panel->top;
		else {
			panel->curs -= display.panlines;
			LIMIT_MIN(panel->curs,panel->min);
			panel->top = panel->curs;
		}
		win_panel_opt();
	}
}

void
cx_pan_pgdown(void)
{
	if (panel->curs < panel->cnt - 1) {
		if (panel->curs < panel->top + display.panlines - 1)
			panel->curs = panel->top + display.panlines - 1;
		else
			panel->curs += display.panlines;
		LIMIT_MAX(panel->curs,panel->cnt - 1);
		LIMIT_MIN(panel->top,panel->curs - display.panlines + 1);
		win_panel_opt();
	}
}

void
pan_adjust(PANEL_DESC *p)
{
	/* always in bounds */
	LIMIT_MAX(p->top,p->cnt - 1);
	LIMIT_MIN(p->top,p->min);
	LIMIT_MAX(p->curs,p->cnt - 1);
	LIMIT_MIN(p->curs,p->min);

	/* cursor must be visible */
	if (p->top > p->curs || p->top <= p->curs - display.panlines)
		p->top = p->curs - display.panlines / 3;
	/* bottom of the screen shouldn't be left blank ... */
	LIMIT_MAX(p->top,p->cnt - display.panlines);
	/* ... but that is not always possible */
	LIMIT_MIN(p->top,p->min);
}
