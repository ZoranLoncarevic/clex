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

#include <sys/types.h>  /* clex.h */
#include <stdlib.h>		/* qsort() */
#include <string.h>		/* strcmp() */

/* major() */
#ifdef MAJOR_IN_MKDEV
# include <sys/mkdev.h>
#endif
#ifdef MAJOR_IN_SYSMACROS
# include <sys/sysmacros.h>
#endif

#include "clex.h"
#include "sort.h"

#include "cfg.h"		/* config_num() */
#include "directory.h"	/* filepos_save() */
#include "sdstring.h"	/* SDSTR() */

void
sort_prepare(void)
{
	panel_sort.pd->top = panel_sort.pd->min;
	panel_sort.pd->curs = 0;	/* sort by name (default) */
	panel = panel_sort.pd;
	textline = 0;
}

void
cx_sort_set(void)
{
	if (panel_sort.order != panel_sort.pd->curs) {
		panel_sort.order = panel_sort.pd->curs;
		filepos_save();
		sort_files();
		filepos_set();
		ppanel_file->other->expired = 1;
	}
	next_mode = MODE_SPECIAL_RETURN;
}

/* compare reversed strings */
static int
revstrcmp(const char *s1, const char *s2)
{
	size_t i1, i2;
	int c1, c2;

	for (i1 = strlen(s1), i2 = strlen(s2); i1 > 0 && i2 > 0;) {
		c1 = (unsigned char)s1[--i1];
		c2 = (unsigned char)s2[--i2];
		/*
		 * ignoring LOCALE, this sort order has nothing to do
		 * with a human language, it is intended for sendmail
		 * queue directories
		 */
		if (c1 != c2)
			return c1 - c2;
	}
	return CMP(i1,i2);
}

/* sort_group() return values (grouping order 1 < 2 < ... < 6 < 7) */
#define GROUP_DOTDIR	1
#define GROUP_DOTDOTDIR	2
#define GROUP_DIR		3
#define GROUP_BDEV		4
#define GROUP_CDEV		5
#define GROUP_OTHER		6
#define GROUP_PLAIN		7
static int
sort_group(FILE_ENTRY *pfe)
{
	int type;

	type = pfe->file_type;
	if (IS_FT_PLAIN(type))
		return GROUP_PLAIN;
	if (IS_FT_DIR(type)) {
		if (pfe->dotdir == 1)
			return GROUP_DOTDIR;
		if (pfe->dotdir == 2)
			return GROUP_DOTDOTDIR;
		return GROUP_DIR;
	}
	if (config_num(CFG_GROUP_FILES) == 2) {
		if (type == FT_DEV_CHAR)
			return GROUP_CDEV;
		if (type == FT_DEV_BLOCK)
			return GROUP_BDEV;
	}
	return GROUP_OTHER;
}

static int
qcmp(const void *e1, const void *e2)
{
	int cmp, group1, group2;
	FILE_ENTRY *pfe1, *pfe2;

	pfe1 = (*(FILE_ENTRY **)e1);
	pfe2 = (*(FILE_ENTRY **)e2);

	/* I. file type grouping */
	if (config_num(CFG_GROUP_FILES) > 0) {
		group1 = sort_group(pfe1);
		group2 = sort_group(pfe2);
		cmp = group1 - group2;
		if (cmp)
			return cmp;

		/* special sorting for devices */
		if (config_num(CFG_GROUP_FILES) == 2
		  && (group1 == GROUP_BDEV || group1 == GROUP_CDEV) ) {
			cmp = major(pfe1->devnum) - major(pfe2->devnum);
			if (cmp)
				return cmp;
			cmp = minor(pfe1->devnum) - minor(pfe2->devnum);
			if (cmp)
				return cmp;
		}
	}

	/* II. sort order */
	switch (panel_sort.order) {
	case SORT_EMAN:
		return revstrcmp(SDSTR(pfe1->file),SDSTR(pfe2->file));
	case SORT_SUFFIX:
		cmp = (config_num(CFG_COLLATION) ? STRCOLL : strcmp)
		  (pfe1->extension,pfe2->extension);
		break;
	case SORT_TIME:
		cmp = CMP(pfe2->mtime,pfe1->mtime);
		break;
	case SORT_TIME_REV:
		cmp = CMP(pfe1->mtime,pfe2->mtime);
		break;
	case SORT_SIZE:
		cmp = CMP(pfe1->size,pfe2->size);
		break;
	case SORT_SIZE_REV:
		cmp = CMP(pfe2->size,pfe1->size);
		break;
	default:	/* SORT_NAME */
		cmp = 0;
	}
	if (cmp)
		return cmp;

	/* III. sort by file name */
	return (config_num(CFG_COLLATION) ? STRCOLL : strcmp)
	  (SDSTR(pfe1->file),SDSTR(pfe2->file));
}

void
sort_files(void)
{
	if (ppanel_file->pd->cnt == 0)
		return;
	qsort(ppanel_file->files,ppanel_file->pd->cnt,sizeof(FILE_ENTRY *),qcmp);
}
