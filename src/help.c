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

/*
 * helpfile format:
 *
 * - helpfile is a plain text file
 * - comments beginning with '#' on the first column are ignored
 * - please no TABs, always use SPACEs
 * - maximal width is 60 columns (MIN_COLS - 4)
 *
 * help file:
 *    @V=VERSION  <-- program version
 *    <page1>
 *    <page2>
 *    ....
 *    <pageX>
 *
 * page:
 *    @P=pgname [@@=heading]    <-- new page named 'pgname' starts here
 *    <line1>                   <-- one or more lines follow
 *    <line2>
 *    ....
 *    <lineY>
 *
 * line:
 *    literal_text [@@=pgname]  <-- line of text with an optional link
 */

#include <config.h>

#include <sys/types.h>	/* clex.h */
#include <errno.h>		/* errno */
#include <stdarg.h>		/* va_list */
#include <stdlib.h>		/* free() */
#include <stdio.h>		/* vprintf() */
#include <string.h>		/* strcmp() */

#include "clex.h"
#include "help.h"

#include "cfg.h"		/* config_str() */
#include "control.h"	/* get_previous_mode() */
#include "inout.h"		/* win_panel() */
#include "panel.h"		/* pan_adjust() */
#include "util.h"		/* emalloc() */

#define HELP_HISTORY_ALLOC	16

/* limits to protect resources */
#define HELP_PAGES_LIMIT	60			/* pages max */
#define HELP_FILESIZE_LIMIT	75000		/* bytes max */

/* help sources: external(optional) and built-in */
static char *ext_help = 0;	/* contents of the optional help file */
static HELP_ENTRY built_in_help[] = {
#	include "help.inc"
	{ 0, 0 }
};								/* built-in help */

/* parsed help */
static HELP_ENTRY *helpline = 0;	/* help split into lines */
static struct {
	const char *name;			/* page name (for links) */
	const char *heading;		/* heading to be displayed */
	int firstline;				/* first line = 'helpline[]' index */
	int size;					/* total number of lines in this page */
	FLAG referred;				/* for consistency check */
} helppage[HELP_PAGES_LIMIT];	/* help lines split into pages */
static int pagecnt;				/* number of pages in 'helppage[]' */
static int tocnum;				/* ToC page number */

/* error handling */
static const char *help_source;	/* name of the help_source */
static FLAG helperror;			/* there was an error */

/* help history - allows only to go back to previous page */
static struct {
	int pagenum, top, curs;
} history[HELP_HISTORY_ALLOC];
static int head, tail;			/* circular buffer indices */
/* increment/decrement (+1/-1) circular buffer index X */
#define MVINDEX(X,N) \
  (X = (X + HELP_HISTORY_ALLOC + N) % HELP_HISTORY_ALLOC)

extern int errno;

static int
page2num(const char *pagename)
{
	int i;

	for (i = 0; i < pagecnt; i++)
		if (strcmp(helppage[i].name,pagename) == 0)
			return i;
	return -1;		/* no such page */
}

static void
help_error_init(const char *filename)
{
	help_source = filename ? filename : "<built-in help>";
	helperror = 0;
}

static void
help_error(const char *format, ...)
{
	va_list argptr;

	if (!TSET(helperror))
		txt_printf("HELP: Reading \"%s\"\n",help_source);

	va_start(argptr,format);
	fputs("HELP: ",stdout);
	vprintf(format,argptr);
	va_end(argptr);
}

/* read external file into memory and split it into lines */
static int
read_external_help(const char *filename)
{
	int error, line;
	char *ptr, *eot;
	size_t filesize;

	filesize = HELP_FILESIZE_LIMIT;
	ext_help = read_file(filename,&filesize,&error);
	if (ext_help == 0) {
		switch (error) {
		case 1:
			help_error("Cannot open the file (%s)\n",strerror(errno));
			break;
		case 2:
			help_error("File is too big, limit is "
			  STR(HELP_FILESIZE_LIMIT) " bytes\n");
			break;
		case 3:
			help_error("File read error (%s)\n",strerror(errno));
		}
		return -1;
	}

	/* count lines + add 1 extra */
	line = 1;
	for (ptr = ext_help; ptr < ext_help + filesize; ptr++)
		if (*ptr == '\t' || *ptr == '\r')
			*ptr = ' ';
		else if (*ptr == '\n' || *ptr == '\0') {
			*ptr = '\0';
			line++;
	}
	helpline = emalloc(line * sizeof(HELP_ENTRY));

	line = 0;
	for (ptr = ext_help; ptr < ext_help + filesize; ) {
		if (*ptr != '#') {
			helpline[line].txt = ptr;
			helpline[line].aux = 0;
			/* split XXXX @@=YYYY, remove trailing spaces after XXXX */
			for (eot = ptr; *ptr; ptr++) {
				if (ptr[0] == '@' && ptr[1] == '@' && ptr[2] == '=') {
					*ptr = *eot = '\0';
					ptr += 3;
					helpline[line].aux = ptr;
					break;
				}
				else if (*ptr != ' ')
					eot = ptr + 1;
			}
			line++;
		}
		/* advance to the next line */
		while (*ptr++)
			;
	}
	/* end of data */
	helpline[line].txt = 0;
	helpline[line].aux = 0;

	return 0;
}


/* parse_help() returns -1 on serious error */
static int
parse_help(const char *filename)
{
	FLAG pagestart;
	const char *ptr, *aux;
	int pg, line;

	/* initialize error handling */
	help_error_init(filename);

	/* erase previous help */
	if (ext_help) {
		free(ext_help);
		ext_help = 0;
	}
	if (helpline) {
		if (helpline != built_in_help)
			free(helpline);
		helpline = 0;
	}

	if (filename == 0)
		helpline = built_in_help;
	else if (read_external_help(filename) < 0)
		return -1;

	/* parse help */
	pagestart = 0;
	pagecnt = 0;
	for (line = 0; (ptr = helpline[line].txt); line++)
		if (ptr[0] == '@' && ptr[1] == 'V' && ptr[2] == '=') {
			/* version stamp */
			if (strcmp(ptr + 3,VERSION)) {
				help_error("Program and help file versions do not"
				  " match; information\n"
				  "      in the on-line help might be inaccurate"
				  " or out-of-date.\n"
				  "      Please consider using the built-in help.\n");
			}
		}
		else if (ptr[0] == '@' && ptr[1] == 'P' && ptr[2] == '=') {
			/* page begin */
			ptr += 3;
			if (pagecnt == HELP_PAGES_LIMIT) {
				help_error("Too many help pages, limit is "
				  STR(HELP_PAGES_LIMIT) "\n");
				return -1;
			}
			if (TSET(pagestart))
				help_error("Page @P=%s is empty\n",
				  helppage[pagecnt - 1].name);
			if ((pg = page2num(ptr)) >= 0) {
				help_error("Page @P=%s was redefined\n",ptr);
				helppage[pg].referred = 1;	/* exclude from checks */
				helppage[pg].name = "@@=";	/* unmatchable */
			}
			helppage[pagecnt].name = ptr;
			aux = helpline[line].aux;
			helppage[pagecnt].heading = aux ? aux : ptr;
			helppage[pagecnt].size = 0;
			helppage[pagecnt].referred = 0;
			pagecnt++;
		}
		else {
			/* literal text */
			if (pagecnt == 0)
				help_error("Bogus text before the first page\n");
			else {
				if (TCLR(pagestart))
					helppage[pagecnt - 1].firstline = line;
				helppage[pagecnt - 1].size++;
				if (strlen(ptr) > MIN_COLS - 4)
					help_error("Line in the @P=%s page is longer "
					  "than %d chars\n",
					  helppage[pagecnt - 1].name,MIN_COLS - 4);
			}
		}

	/*** consistency checks ***/

	/* A. broken links */
	for (line = 0; (ptr = helpline[line].txt); line++)
		if ( (aux = helpline[line].aux) ) {
			if (ptr[0] == '@' && (ptr[1] == 'P' || ptr[1] == 'V')
			  && ptr[2] == '=')
				continue;	/* not a link */
			pg = page2num(aux);
			if (pg >= 0)
				helppage[pg].referred = 1;
			else
				help_error("Broken link: @@=%s\n",aux);
		}

	/* B. ToC must exist */
	if ( (tocnum = page2num("ToC")) < 0) {
		help_error("Table of contents @P=ToC is missing\n");
		return -1;
	}
	helppage[tocnum].referred = 1;

	/* C. abandoned pages */
	for (pg = 0; pg < pagecnt; pg++)
		if (!helppage[pg].referred)
			help_error("Abandoned page: @P=%s\n",helppage[pg].name);
		
	return 0;
}

void
help_reconfig(void)
{
	if (*config_str(CFG_HELPFILE)) {
		/* external help */
		if (parse_help(config_str(CFG_HELPFILE)) == 0)
			return;
		help_error("Using the built-in help instead\n");
	}

	parse_help(0);
	/* built-in help: each error is fatal */
	if (helperror)
		err_exit("BUG: built-in help is incorrect");
}

static void
set_page(int pg)
{
	int i;

	panel_help.pd->min = pg == tocnum ? 0 : -1;
	panel_help.pd->top = panel_help.pd->curs = panel_help.pd->min;
	panel_help.pd->cnt = helppage[pg].size;
	panel_help.pagenum = pg;
	panel_help.heading = helppage[pg].heading;
	if (panel_help.pd->cnt) {
		panel_help.line = helpline + helppage[pg].firstline;
		for (i = 0; i < panel_help.pd->cnt; i++)
			if (panel_help.line[i].txt[0] != '\0') {
				panel_help.pd->curs = i;
				pan_adjust(panel_help.pd);
				break;
			}
	}
	win_heading();
}

static void
help_goto(const char *pagename)
{
	int pg;

	if ( (pg = page2num(pagename)) < 0) {
		/* ERROR 404 :-) */
		win_warning_fmt("HELP: help-page '%s' not found.",pagename);
		return;
	}

	/* save current page parameters */
	history[head].pagenum = panel_help.pagenum;
	history[head].top = panel_help.pd->top;
	history[head].curs = panel_help.pd->curs;
	MVINDEX(head,1);
	if (head == tail)
		MVINDEX(tail,1);

	/* new page */
	set_page(pg);

	win_panel();
}

void
help_prepare(void)
{
	const char *page;
	int pg;

	head = tail = 0;

	/* CLEX help is context sensitive */
	switch (get_previous_mode()) {
	case MODE_BM_LIST:
	case MODE_BM_MANAGER:
	case MODE_BM_EDIT:
		page = "bookmarks";
		break;
	case MODE_CFG:
	case MODE_CFG_EDIT_NUM:
	case MODE_CFG_EDIT_TXT:
		page = "config";
		break;
	case MODE_COMPARE:
		page = "compare";
		break;
	case MODE_COMPL:
		page = "completion";
		break;
	case MODE_DIR:
	case MODE_DIR_SPLIT:
		page = "dir";
		break;
	case MODE_FILE:
		page = "file";
		break;
	/* there is no case MODE_HELP: */
	case MODE_HIST:
		page = "history";
		break;
	case MODE_MAINMENU:
		page = "menu";
		break;
	case MODE_PASTE:
		page = "paste";
		break;
	case MODE_SELECT:
	case MODE_DESELECT:
		page = "select";
		break;
	case MODE_SORT:
		page = "sort";
		break;
	case MODE_USER:
	case MODE_GROUP:
		page = "user";
		break;
	default:
		page = "ToC";
	}

	pg = page2num(page);
	if (pg < 0)
		pg = tocnum;
	set_page(pg);

	panel = panel_help.pd;
	textline = 0;
}

/* follow link */
void
cx_help_link(void)
{
	const char *link;

	link = VALID_CURSOR(panel_help.pd) ?
	  panel_help.line[panel_help.pd->curs].aux : "ToC";
	if (link)
		help_goto(link);
}

/* display table of contents */
void
cx_help_contents(void)
{
	if (panel_help.pagenum != tocnum)
		help_goto("ToC");
}

/* go back to previous page */
void cx_help_back(void)
{
	if (head == tail) {
		win_remark("there is no previous help-page");
		return;
	}

	MVINDEX(head,-1);
	set_page(history[head].pagenum);
	panel_help.pd->top = history[head].top;
	panel_help.pd->curs = history[head].curs;
	pan_adjust(panel_help.pd);
	win_panel();
}
