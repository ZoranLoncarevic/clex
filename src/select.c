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
#include <sys/stat.h>	/* stat() */
#include <fcntl.h>		/* open() */
#include <stdlib.h>		/* qsort() */
#include <string.h>		/* strcpy() */
#include <unistd.h>		/* close() */

#include "clex.h"
#include "select.h"

#include "control.h"	/* get_current_mode() */
#include "edit.h"		/* edit_putstr() */
#include "inout.h"		/* win_panel() */
#include "match.h"		/* match() */
#include "list.h"		/* list_both_directories() */
#include "sdstring.h"	/* SDSTR() */
#include "sort.h"		/* sort_files() */
#include "ustring.h"	/* USTR() */
#include "util.h"		/* pathname_join() */

#define FN_SELECT	1
#define FN_DESELECT	2
#define FN_TOGGLE	3

void
select_prepare(void)
{
	static FLAG hint = 1;
	const char *prompt;

	panel = ppanel_file->pd;
	if (panel->filtering == 1)
		panel->filtering = 2;

	prompt = "DESELECT files: ";
	if (get_current_mode() == MODE_SELECT)
		prompt += 2;	/* "SELECT files: " */
	edit_setprompt(&line_tmp,prompt);
	textline = &line_tmp;
	edit_nu_putstr("*");

	if (TCLR(hint))
		win_remark("wildcards: ? * and [..], see help");
}

void
compare_prepare(void)
{
	/* leave cursor position unchanged */
	panel = panel_compare.pd;
	textline = 0;
}

static int
selectfile(FILE_ENTRY *pfe, int function)
{
	if (!pfe->select) {
		/*
		 * dot and dot-dot directories cannot be selected
		 * it is not a bug, it is a protection
		 */
		if (function != FN_DESELECT && !pfe->dotdir) {
			pfe->select = 1;	/* one selected entry more */
			return 1;
		}
	}
	else if (function != FN_SELECT) {
		pfe->select = 0;
		return -1;				/* one less */
	}
	return 0;					/* no change */
}

void
cx_select_toggle(void)
{
	ppanel_file->selected +=
	  selectfile(ppanel_file->files[ppanel_file->pd->curs],FN_TOGGLE);

	/* cursor down */
	if (ppanel_file->pd->curs < ppanel_file->pd->cnt - 1) {
		ppanel_file->pd->curs++;
		LIMIT_MIN(ppanel_file->pd->top,
		  ppanel_file->pd->curs - display.panlines + 1);
	}

	win_panel_opt();
}

static void
process_all(int function)
{
	int i;

	for (i = 0; i < ppanel_file->pd->cnt; i++)
		ppanel_file->selected +=
		  selectfile(ppanel_file->files[i],function);
	win_panel();
}

void cx_select_invert(void)		{ process_all(FN_TOGGLE);   }
void cx_select_allfiles(void)	{ process_all(FN_SELECT);   }
void cx_select_nofiles(void)	{ process_all(FN_DESELECT); }

void
cx_select_files(void)
{
	int i, fn;
	FLAG select;
	const char *sre;
	FILE_ENTRY *pfe;

	if (line_tmp.size == 0) {
		next_mode = MODE_SPECIAL_RETURN;
		return;
	}
	sre = USTR(textline->line);
	if (match_sre(sre) < 0)
		/* return and correct the error */
		return;

	if (get_current_mode() == MODE_SELECT) {
		select = 1; fn = FN_SELECT;
	}
	else {
		select = 0; fn = FN_DESELECT;
	}
	for (i = 0; i < ppanel_file->pd->cnt; i++) {
		pfe = ppanel_file->files[i];
		if (select == !pfe->select && match(SDSTR(pfe->file)))
			ppanel_file->selected += selectfile(pfe,fn);
	}
	win_panel();
	next_mode = MODE_SPECIAL_RETURN;
}

static int
qcmp(const void *e1, const void *e2)
{
	/* not strcoll() ! */
	return strcmp(
	  SDSTR((*(FILE_ENTRY **)e1)->file),
	  SDSTR((*(FILE_ENTRY **)e2)->file));
}

#define CMP_BUF_STR	16384

/* return value: -1 error, 0 compare ok, +1 compare failed */
static int
data_cmp(int fd1, int fd2)
{
	struct stat st1, st2;
	static char buff1[CMP_BUF_STR], buff2[CMP_BUF_STR];
	off_t filesize;
	size_t chunksize;

	if (fstat(fd1,&st1) < 0 || !S_ISREG(st1.st_mode))
		return -1;
	if (fstat(fd2,&st2) < 0 || !S_ISREG(st2.st_mode))
		return -1;
	if (st1.st_dev == st2.st_dev && st1.st_ino == st2.st_ino)
		/* same file */
		return 0;
	if ((filesize = st1.st_size) != st2.st_size)
		return 1;

	while (filesize > 0) {
		chunksize = filesize > CMP_BUF_STR ? CMP_BUF_STR : filesize;
		if ( read_fd(fd1,buff1,chunksize) != chunksize
		  || read_fd(fd2,buff2,chunksize) != chunksize)
			return -1;
		if (memcmp(buff1,buff2,chunksize) != 0)
			return 1;
		filesize -= chunksize;
	}

	return 0;
}

/* return value: -1 error, 0 compare ok, +1 compare failed */
static int
file_cmp(const char *file1, const char *file2)
{
	int cmp, fd1, fd2;

	fd1 = open(file1,O_RDONLY | O_NONBLOCK);
	if (fd1 < 0)
		return -1;
	fd2 = open(file2,O_RDONLY | O_NONBLOCK);
	if (fd2 < 0) {
		close(fd1);
		return -1;
	}
	cmp = data_cmp(fd1,fd2);
	close(fd1);
	close(fd2);
	return cmp;
}

/*
 * compare panels
 * levels:
 *	0: name, type       (symbolic links ok)
 *	1: name, type, size (symbolic links ok)
 *	2: name, type, size (exactly)
 *	3: name, type, size, ownership&mode
 *	4: name, type, size, contents
 *	5: name, type, size, ownership&mode, contents
 */
void
compare_panels(int level)
{
	int min, med, max, cmp, i, cnt1, errcnt;
	const char *name2;
	FILE_ENTRY *pfe1, *pfe2;

	errcnt = 0;

	/* reread panels */
	list_both_directories();

	if (level >= 4) {
		win_waitmsg();
		pathname_set_directory(USTR(ppanel_file->other->dir));
	}

	/* sort panel#1 by name (prepare for binary search) */
	if ( (cnt1 = ppanel_file->pd->cnt) )
		qsort(ppanel_file->files,cnt1,sizeof(FILE_ENTRY *),qcmp);

	/* select all files in the panel#1 */
	for (i = 0; i < cnt1; i++)
		ppanel_file->selected +=
		  selectfile(ppanel_file->files[i],FN_SELECT);

	/* for all files in panel#2 search for matching file in panel#1 */
	for (i = 0; i < ppanel_file->other->pd->cnt; i++) {
		pfe2 = ppanel_file->other->files[i];
		ppanel_file->other->selected += selectfile(pfe2,FN_SELECT);

		/* all levels: comparing name */
		name2 = SDSTR(pfe2->file);
		for (pfe1 = 0, min = 0, max = cnt1 - 1; min <= max; ) {
			med = (min + max) / 2;
			cmp = strcmp(name2,SDSTR(ppanel_file->files[med]->file));
			if (cmp == 0) {
				pfe1 = ppanel_file->files[med];
				/* entries *pfe1 and *pfe2 have the same name */
				break;
			}
			if (cmp < 0)
				max = med - 1;
			else
				min = med + 1;
		}
		if (pfe1 == 0)
			continue;

		/* all levels: comparing type */
		if ( !( (IS_FT_PLAIN(pfe1->file_type)
			&& IS_FT_PLAIN(pfe2->file_type)) ||
		  (IS_FT_DIR(pfe1->file_type) &&
			IS_FT_DIR(pfe2->file_type)) ||
		  (pfe1->file_type == pfe2->file_type
			&& pfe1->file_type != FT_NA)
		  ))
			continue;

		/* level 1+: comparing size (or device numbers) */
		if (level >= 1
		  && ((IS_FT_DEV(pfe1->file_type) && pfe1->devnum != pfe2->devnum)
		  || (IS_FT_PLAIN(pfe1->file_type) && pfe1->size != pfe2->size)))
			continue;

		/* level 2+: symbolic links not ok */
		if (level >= 2 && pfe1->symlink != pfe2->symlink)
			continue;

		/* level 3,5+: ownership and mode */
		if ((level == 3 || level >= 5) && (pfe1->uid != pfe2->uid
		  || pfe1->gid != pfe2->gid || pfe1->mode12 != pfe2->mode12))
			continue;

		/* level 4+: comparing data (contents) */
		if (level >= 4 && IS_FT_PLAIN(pfe1->file_type)
		  && (cmp = file_cmp(SDSTR(pfe1->file),pathname_join(name2))) ) {
			if (cmp < 0 && ++errcnt <= 3)
				win_warning_fmt("COMPARE: Cannot read file '%s'.",
				  name2);
			continue;
		}

		/* pair of matching files found */
		ppanel_file->selected +=
		  selectfile(pfe1,FN_DESELECT);
		ppanel_file->other->selected +=
		  selectfile(pfe2,FN_DESELECT);
	}

	if (errcnt > 3)
		win_warning_fmt("COMPARE: %d files could not be read.",errcnt);

	/* restore the sort order */
	sort_files();

	win_panel();
}

static void
compare_level(int level)
{
	compare_panels(level);
	next_mode = MODE_SPECIAL_RETURN;
}

void cx_compare(void)   { compare_level(panel_compare.pd->curs); }
