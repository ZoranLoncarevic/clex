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

#include <sys/types.h>		/* time_t */
#include <sys/stat.h>		/* stat() */
#include <errno.h>			/* errno */
#include <filter.h>			/* filter_update() */
#include <stdio.h>			/* sprintf() */
#include <string.h>			/* strcmp() */
#include <unistd.h>			/* stat() */

/* struct tm, time() */
#if TIME_WITH_SYS_TIME
# include <sys/time.h>
# include <time.h>
#else
# if HAVE_SYS_TIME_H
#  include <sys/time.h>
# else
#  include <time.h>
# endif
#endif

/* readdir() */
#ifdef HAVE_DIRENT_H
# include <dirent.h>
#else
# define dirent direct
# ifdef HAVE_SYS_NDIR_H
#  include <sys/ndir.h>
# endif
# ifdef HAVE_SYS_DIR_H
#  include <sys/dir.h>
# endif
# ifdef HAVE_NDIR_H
#  include <ndir.h>
# endif
#endif

/* major() */
#ifdef MAJOR_IN_MKDEV
# include <sys/mkdev.h>
#endif
#ifdef MAJOR_IN_SYSMACROS
# include <sys/sysmacros.h>
#endif

#ifndef S_ISLNK
# define S_ISLNK(X)	(0)
#endif

#include "clex.h"
#include "list.h"

#include "cfg.h"			/* config_num() */
#include "directory.h"		/* filepos_save() */
#include "inout.h"			/* win_warning() */
#include "lang.h"			/* lang_sep000 */
#include "sdstring.h"		/* SDSTR() */
#include "sort.h"			/* sort_files() */
#include "userdata.h"		/* lookup_login() */
#include "ustring.h"		/* USTR() */
#include "util.h"			/* emalloc() */

/*
 * additional FILE_ENTRIES to be allocated when the file panel is full,
 * i.e. when all existing entries in 'files' are occupied
 */
#define FE_ALLOC_UNIT	128

#define CACHE_SIZE 		24	/* size of cache for user/group name lookups */

extern int errno;

static time_t now;
static FLAG do_a, do_d, do_i, do_l, do_L, do_m, do_M, do_o, do_s;
static FLAG clock24, use_pathname = 0;
static char sep000;			/* thousands separator */
static int tpad, dpad;		/* length of padding for time and date */
static int K2;				/* kilobyte/2 */
static const char *fmt_date;/* format string for date */
static dev_t dirdev;		/* data of the inspected directory */
static mode_t normal_file, normal_dir;
static int ucache_cnt = 0, gcache_cnt = 0;

void
list_reconfig(void)
{
	const char *fields;
	FLAG fld;
	char ch;
	int i, dlen, tlen;

	/* KILOBYTE */
	K2 = config_num(CFG_KILOBYTE) ? 500 : 512;

	/* FMT_DATE */
	fmt_date = config_str(CFG_FMT_DATE);
	if (*fmt_date == '\0')
		fmt_date = lang_fmt_date();
	/* compute the field length */
	for (dlen = i = 0; (ch = fmt_date[i]); i++)
		switch (ch) {
		case 'Y': dlen += 4; break;
		case 'M': dlen += 3; break;
		case 'y':
		case 'm':
		case 'd':
		case 'D': dlen += 2; break;
		default:  dlen += 1;
	}
	if (dlen >= FE_TIME_STR) {
		txt_printf("FMT_DATE: "
		  "date string is too long, using \"dMy\" format\n");
		fmt_date="dMy";
		dlen = 7;
	}

	/* FMT_TIME */
	switch (config_num(CFG_FMT_TIME)) {
		case 0:
			clock24 = lang_clock24();
			break;
		case 1:
			clock24 = 0;
			break;
		case 2:
			clock24 = 1;
			break;
	}
	tlen = clock24 ? 5 : 7;	/* DD:MM or DD:MMam */

	/*
	 * time string and date string are displayed in the same column,
	 * prepare padding string to equalize their lengths
	 */
	if (tlen >= dlen) {
		tpad = 0;
		dpad = tlen - dlen;
		display.date_len = tlen;
	}
	else {
		dpad = 0;
		tpad = dlen - tlen;
		display.date_len = dlen;
	}

	/* FMT_NUMBER */
	switch (config_num(CFG_FMT_NUMBER)) {
		case 0:
			sep000 = lang_sep000();
			break;
		case 1:
			sep000 = '.' ;
			break;
		case 2:
			sep000 = ',' ;
			break;
	}

	/* which fields are going to be displayed ? */
	do_a = do_d = do_i = do_l = 0;
	do_L = do_m = do_M = do_o = do_s = 0;
	for (fld = 0, fields = config_layout; (ch = *fields++); )
		if (!TCLR(fld)) {
			if (ch == '$')
				fld = 1;
		}
		else {
			switch (ch) {
			case 'a': do_a = 1; break;
			case 'd': do_d = 1; break;
			case 'i': do_i = 1; break;
			case 'L': do_L = 1; break;
			case 'l': do_l = 1; break;
			case 'P':					/* $P -> $M */
			case 'M': do_M = 1; 		/* $M -> $m */
			case 'p':					/* $p -> $m */
			case 'm': do_m = 1; break;
			case 'o': do_o = 1; break;
			case 'S':					/* $S -> $s */
			case 's': do_s = 1; break;
			/* default:   ignore unknown formatting character */
			}
		}
}

void
list_initialize(void)
{
	normal_dir  = 0777 & ~clex_data.umask;	/* dir or executable file */
	normal_file = 0666 & ~clex_data.umask;	/* any other file */

	list_reconfig();
}

/*
 * when calling any of the stat2xxx() functions always make sure
 * the string provided as 'str' argument can store the result.
 * Check the FE_XXX_STR #defines in clex.h for proper sizes.
 */

static void
stat2time(char *str, time_t tm)
{
	int i, len, hour;
	char ch;
	const char *ampm;
	struct tm *ptm;
	static char *month[] = {
		"jan", "feb", "mar", "apr", "may", "jun",
		"jul", "aug", "sep", "oct", "nov", "dec"
	};

	ptm = localtime(&tm);
	/* print time of day, after 18 hrs (64800s) print date */
	/* 5 mins (300s) tolerance */
	if (tm <= now + 300 && now <= tm + 64800) {
		if (clock24) {
			hour = ptm->tm_hour;
			ampm = "";
		}
		else {
			hour = 1 + (ptm->tm_hour + 11) % 12;
			ampm = ptm->tm_hour < 12 ? "am" : "pm";
		}
		for (len = 0; len < tpad; len++)
			*str++ = ' ';
		sprintf(str,"%02d:%02d%s",hour,ptm->tm_min,ampm);
	}
	else {
		for (len = 0; len < dpad; len++)
			*str++ = ' ';
		for (i = 0; (ch = fmt_date[i]); i++) {
			switch (ch) {
			case 'Y':
				/* Y10K compliant ;-) */
				sprintf(str,"%04d",(1900 + ptm->tm_year) % 10000);
				str += 4;
				break;
			case 'y':
				sprintf(str,"%02d",ptm->tm_year % 100);
				str += 2;
				break;
			case 'M':
				strcpy(str,month[ptm->tm_mon]);
				str += 3;
				break;
			case 'm':
				sprintf(str,"%02d",ptm->tm_mon + 1);
				str += 2;
				break;
			case 'd':
				sprintf(str,"%02d",ptm->tm_mday);
				str += 2;
				break;
			case 'D':
				sprintf(str,"%2d",ptm->tm_mday);
				str += 2;
				break;
			default:
				*str++ = ch;
				*str   = '\0';
			}
		}
	}
}

static void
stat2size(char *str, off_t size)
{
	int exp, roundup;

	for (exp = roundup = 0; size > 9999999 /* 9.999.999 */; exp++) {
		/* size = size / 1K */
		size /= K2;
		roundup = size % 2;
		size /= 2;
	}

	sprintf(str,"  %7ld%c ",(long int)(size + roundup)," KMGTPEZY"[exp]);
	/* insert thousands separators: 1.234.567 */
	if (str[5] != ' ') {
		if (str[2] != ' ') {
			str[0] = str[2];
			str[1] = sep000;
		}
		str[2] = str[3];
		str[3] = str[4];
		str[4] = str[5];
		str[5] = sep000;
	}
	if (exp && K2 == 512)
		str[10] = 'i';
}

/*
 * stat2dev() prints device major:minor numbers
 * 
 * FE_SIZE_DEV_STR is 12 ==> total number of digits is 10,
 * from these 10 digits are 2 to 7 used for minor device
 * number (printed in hex) and the rest is used for major
 * device number (printed in dec)
 *
 * some major:minor splits
 *    8 :  8 bits - Linux 2.4
 *   14 : 18 bits - SunOS 5
 *    8 : 24 bits - FreeBSD
 *   12 : 20 bits - Linux 2.6
 */

#define MIN_MINOR_DIGITS	2
#define MAX_MINOR_DIGITS	7

static void
stat2dev(char *str, unsigned int dev_major, unsigned int dev_minor)
{
	static unsigned int digits_minor[] = {
	  0,
	  0xF,
	  0xFF,		/* 2 digits,  8 bits */
	  0xFFF,	/* 3 digits, 12 bits */
	  0xFFFF,	/* 4 digits, 16 bits */
	  0xFFFFF,	/* 5 digits, 20 bits */
	  0xFFFFFF,	/* 6 digits, 24 bits */
	  0xFFFFFFF,/* 7 digits, 28 bits */
	  0xFFFFFFFF
	};
	static unsigned int digits_major[] = {
      0,
	  9,
	  99,
	  999, 		/* 3 digits,  9 bits */
	  9999,		/* 4 digits, 13 bits */
	  99999,	/* 5 digits, 16 bits */
	  999999,	/* 6 digits, 19 bits */
	  9999999,	/* 7 digits, 23 bits */
	  99999999, /* 8 digits, 26 bits */
	  999999999
	};
	static int minor_len = MIN_MINOR_DIGITS;
	static int major_len = FE_SIZE_DEV_STR - MIN_MINOR_DIGITS - 2;
	int minor_of;	/* overflow */

	/* determine the major digits / minor digits split */
	while ( (minor_of = dev_minor > digits_minor[minor_len])
	  && minor_len < MAX_MINOR_DIGITS) {
		minor_len++;
		major_len--;
	}

	/* print major */
	if (dev_major > digits_major[major_len])
		sprintf(str,"%*s",major_len,"..");
	else
		sprintf(str,"%*d",major_len,dev_major);

	/* print minor */
	if (minor_of)
		sprintf(str + major_len,":..%0*X",
		  minor_len - 2,dev_minor & digits_minor[minor_len - 2]);
	else
		sprintf(str + major_len,":%0*X",minor_len,dev_minor);
}

int
stat2type(mode_t mode, uid_t uid)
{
	if (S_ISREG(mode)) {
		if ( (mode & S_IXUSR) != S_IXUSR
		  && (mode & S_IXGRP) != S_IXGRP
		  && (mode & S_IXOTH) != S_IXOTH)
			return FT_PLAIN_FILE;
		if ((mode & S_ISUID) == S_ISUID)
			return uid ? FT_PLAIN_SUID : FT_PLAIN_SUID_ROOT;
		if ((mode & S_ISGID) == S_ISGID)
			return FT_PLAIN_SGID;
		return FT_PLAIN_EXEC;
	}
	if (S_ISDIR(mode))
		return FT_DIRECTORY;
	if (S_ISBLK(mode))
		return FT_DEV_BLOCK;
	if (S_ISCHR(mode))
		return FT_DEV_CHAR;
	if (S_ISFIFO(mode))
		return FT_FIFO;
#ifdef S_ISSOCK
	if (S_ISSOCK(mode))
		return FT_SOCKET;
#endif
	return FT_OTHER;
}

static void
id2name(char *str, int leftalign, const char *name, unsigned int id)
{
	char number[16];
	size_t len;

	if (name == 0) {
		sprintf(number,"%u",id);
		name = number;
	}
	len = strlen(name);
	sprintf(str,len <= 9 ? (leftalign ? "%-9s" : "%9s") : "%4.4s>%4s",name,name + len - 4);

}

static const char *
uid2name(uid_t uid)
{
	static int pos = 0, replace = 0;
	static struct {
		uid_t uid;
		char name[FE_NAME_STR];
	} cache[CACHE_SIZE];

	if (pos < ucache_cnt && uid == cache[pos].uid)
		return cache[pos].name;

	for (pos = 0; pos < ucache_cnt; pos++)
		if (uid == cache[pos].uid)
			return cache[pos].name;

	if (ucache_cnt < CACHE_SIZE)
		pos = ucache_cnt++;
	else {
		pos = replace;
		if (++replace >= CACHE_SIZE)
			replace = 0;
	}
	cache[pos].uid = uid;
	id2name(cache[pos].name,0,lookup_login(uid),(unsigned int)uid);

	return cache[pos].name;
}

static const char *
gid2name(gid_t gid)
{
	static int pos = 0, replace = 0;
	static struct {
		gid_t gid;
		char name[FE_NAME_STR];
	} cache[CACHE_SIZE];

	if (pos < gcache_cnt && gid == cache[pos].gid)
		return cache[pos].name;

	for (pos = 0; pos < gcache_cnt; pos++)
		if (gid == cache[pos].gid)
			return cache[pos].name;

	if (gcache_cnt < CACHE_SIZE)
		pos = gcache_cnt++;
	else {
		pos = replace;
		if (++replace >= CACHE_SIZE)
			replace = 0;
	}
	cache[pos].gid = gid;
	id2name(cache[pos].name,1,lookup_group(gid),(unsigned int)gid);

	return cache[pos].name;
}

static void
stat2owner(char *str, uid_t uid, gid_t gid)
{
		strcpy(str,uid2name(uid));
		str[FE_NAME_STR - 1] = ':';
		strcpy(str + FE_NAME_STR,gid2name(gid));
}

static void
stat2links(char *str, nlink_t nlink)
{
	if (nlink <= 999)
		sprintf(str,"%3d",(int)nlink);
	else
		strcpy(str,"max");
}

/*
 * get the extension "ext" from "file.ext"
 * an exception: ".file" is a hidden file without an extension
 */
static const char *
get_ext(const char *filename)
{
	const char *ext;
	char ch;

	if (*filename++ == '\0')
		return "";

	for (ext = ""; (ch = *filename); filename++)
		if (ch == '.')
			ext = filename + 1;
	return ext;
}

/* this file does exist, but no other information is available */
static void
nofileinfo(FILE_ENTRY *pfe)
{
	pfe->mtime = 0;
	pfe->size = 0;
	pfe->extension = get_ext(SDSTR(pfe->file));
	pfe->file_type = FT_NA;
	pfe->size_str[0] = '\0';
	pfe->atime_str[0] = '\0';
	pfe->mtime_str[0] = '\0';
	pfe->ctime_str[0] = '\0';
	pfe->links_str[0] = '\0';
	pfe->links = 0;
	pfe->mode_str[0] = '\0';
	pfe->normal_mode = 1;
	pfe->owner_str[0] = '\0';
}

/* fill-in all required information about a file */
static void
fileinfo(FILE_ENTRY *pfe, struct stat *pst)
{
	pfe->mtime = pst->st_mtime;
	pfe->size = pst->st_size;
	pfe->extension = get_ext(SDSTR(pfe->file));
	pfe->file_type = stat2type(pst->st_mode,pst->st_uid);
	if (IS_FT_DEV(pfe->file_type))
#ifdef HAVE_STRUCT_STAT_ST_RDEV
		pfe->devnum = pst->st_rdev;
#else
		pfe->devnum = 0;
#endif
	/* special case: active mounting point */
	if (pfe->file_type == FT_DIRECTORY && !pfe->symlink
	  && !pfe->dotdir && pst->st_dev != dirdev)
		pfe->file_type = FT_DIRECTORY_MNT;

	if (do_a)
		stat2time(pfe->atime_str,pst->st_atime);
	if (do_d)
		stat2time(pfe->mtime_str,pst->st_mtime);
	if (do_i)
		stat2time(pfe->ctime_str,pst->st_ctime);
	if (do_l)
		stat2links(pfe->links_str,pst->st_nlink);
	if (do_L)
		pfe->links = pst->st_nlink > 1 && !IS_FT_DIR(pfe->file_type);
	pfe->mode12 = pst->st_mode & 07777;
	if (do_m) {
		sprintf(pfe->mode_str,"%04o",pfe->mode12);
		if (do_M) {
			if (S_ISREG(pst->st_mode))
				pfe->normal_mode = pfe->mode12 == normal_file
				  || pfe->mode12 == normal_dir /* same as exec */;
			else if (S_ISDIR(pst->st_mode))
				pfe->normal_mode = pfe->mode12 == normal_dir;
			else
				pfe->normal_mode = pfe->mode12 == normal_file;
		}
	}
	pfe->uid = pst->st_uid;
	pfe->gid = pst->st_gid;
	if (do_o)
		stat2owner(pfe->owner_str,pst->st_uid,pst->st_gid);
	if (do_s) {
		if (IS_FT_DEV(pfe->file_type))
			stat2dev(pfe->size_str,
			  major(pfe->devnum),minor(pfe->devnum));
		else
			stat2size(pfe->size_str,pst->st_size);
	}
}

/* build the FILE_ENTRY '*pfe' describing the file named 'name' */
static int
describe_file(const char *name, FILE_ENTRY *pfe)
{
	struct stat stdata;

	if (lstat(name,&stdata) < 0) {
		if (errno == ENOENT)
			return -1;		/* file deleted in the meantime */
		pfe->symlink = 0;
		nofileinfo(pfe);
		return 0;
	}

	if ( (pfe->symlink = S_ISLNK(stdata.st_mode)) ) {
		if (get_link_us(&pfe->link,name) < 0)
			us_copy(&pfe->link,"??");
		/* need stat() instead of lstat() */
		if (stat(name,&stdata) < 0) {
			nofileinfo(pfe);
			return 0;
		}
	}

	fileinfo(pfe,&stdata);
	return 0;
}

#define DOT_NONE                0       /* not a .file */
#define DOT_DIR                 1       /* dot directory */
#define DOT_DOT_DIR             2       /* dot-dot directory */
#define DOT_HIDDEN              3       /* hidden .file */

static int
dotfile(const char *name)
{
        if (name[0] != '.')
                return DOT_NONE;
        if (name[1] == '\0')
                return DOT_DIR;
        if (name[1] == '.' && name[2] == '\0')
                return DOT_DOT_DIR;
        return DOT_HIDDEN;
}

/*
 * We abandoned any form of caching and always build the file
 * panel from scratch. No caching algorithm was 100% perfect,
 * there were always few 'pathological' cases.
 */
static void
directory_read(void)
{
	int i, cnt1, cnt2;
	CODE dotdir;
	DIR *dd;
	FILE_ENTRY *pfe;
	FLAG hide;
	struct stat st;
	struct dirent *direntry;
	const char *name;

	name = USTR(ppanel_file->dir);
	if (stat(name,&st) < 0 || (dd = opendir(name)) == 0) {
		ppanel_file->pd->cnt = ppanel_file->selected = 0;
		win_warning("LIST DIR: Cannot list the contents "
		  "of the directory.");
		return;
	}
	dirdev = st.st_dev;
	hide = config_num(CFG_SHOW_HIDDEN) == HIDE_ALWAYS
		|| (config_num(CFG_SHOW_HIDDEN) == HIDE_HOME
		    && strcmp(USTR(ppanel_file->dir),clex_data.homedir) == 0);

	/*
	 * step #1: process selected files already listed in the panel
	 * in order not to lose their selection mark
	 */
	cnt1 = 0;
	for (i = 0; cnt1 < ppanel_file->selected; i++) {
		pfe = ppanel_file->files[i];
		if (!pfe->select)
			continue;
		name = SDSTR(pfe->file);
		if (describe_file(use_pathname ? pathname_join(name) : name,
		  pfe) < 0 || (hide && dotfile(name) == DOT_HIDDEN))
			/* this entry is no more valid */
			ppanel_file->selected--;
		else {
			/* OK, move it to the end of list we have so far */
			/* by swapping pointers: [cnt1] <--> [i] */
			ppanel_file->files[i] = ppanel_file->files[cnt1];
			ppanel_file->files[cnt1] = pfe;
			cnt1++;
		}
	}

	/* step #2: add data about new files */
	win_waitmsg();
	cnt2 = cnt1;
	while ( (direntry = readdir(dd)) ) {
		name = direntry->d_name;
		dotdir = dotfile(name);

		/* didn't we see this file already in step #1 ? */
		if (cnt1) {
			for (i = 0; i < cnt1; i++)
				if (strcmp(SDSTR(ppanel_file->files[i]->file),name)
				  == 0)
					break;
			if (i < cnt1)
				continue;
		}

		/* allocate new bunch of FILE_ENTRies if needed */
		if (cnt2 == ppanel_file->fe_alloc) {
			ppanel_file->fe_alloc += FE_ALLOC_UNIT;
			ppanel_file->files = erealloc(ppanel_file->files,
			  ppanel_file->fe_alloc * sizeof(FILE_ENTRY *));
			pfe = emalloc(FE_ALLOC_UNIT * sizeof(FILE_ENTRY));
			for (i = 0; i < FE_ALLOC_UNIT; i++) {
				SD_INIT(pfe[i].file);
				US_INIT(pfe[i].link);
				ppanel_file->files[cnt2 + i] = pfe + i;
			}
		}

		pfe = ppanel_file->files[cnt2];
		sd_copy(&pfe->file,name);
		pfe->dotdir = (dotdir == DOT_HIDDEN) ? DOT_NONE : dotdir;
		if (describe_file(use_pathname ? pathname_join(name) : name,
		  pfe) < 0 || (hide && dotfile(name) == DOT_HIDDEN))
			continue;
		pfe->select = 0;
		cnt2++;
	}
	ppanel_file->pd->cnt = cnt2;

	closedir(dd);
}

/* directory read wrapper */
static void
filepanel_read(void)
{
	filepos_save();
	if (ppanel_file->pd->filtering) {
		/* suspend filtering */
		ppanel_file->pd->cnt = ppanel_file->filt_cnt;
		ppanel_file->selected += ppanel_file->filt_sel;
	}
	directory_read();
	if (ppanel_file->pd->filtering) {
		/* resume filtering */
		ppanel_file->filt_cnt = ppanel_file->pd->cnt;
		ppanel_file->pd->filter->changed = 1;
	}
	else
		sort_files();
	filepos_set();
	ppanel_file->expired = 0;
}

void
list_directory(void)
{
	now = time(0);

	/* password data change invalidates data in both panels */
	if (userdata_refresh()) {
		ppanel_file->other->expired = 1;
		ucache_cnt = gcache_cnt = 0;
	}

	filepanel_read();
}

void
list_both_directories(void)
{
	PANEL_DESC *savep;

	savep = panel;

	now = time(0);
	if (userdata_refresh())
		ucache_cnt = gcache_cnt = 0;

	filepanel_read();
	panel = ppanel_file->pd;
	if (panel->filtering)
		filter_update();

	/*
	 * warning: during the re-read of the secondary panel,
	 * the primary panel does not correspond with the current
	 * working directory
	 */
	ppanel_file = ppanel_file->other;
	pathname_set_directory(USTR(ppanel_file->dir));
	use_pathname = 1;	/* must prepend directory name */
	filepanel_read();
	panel = ppanel_file->pd;
	if (panel->filtering)
		filter_update();
	use_pathname = 0;
	ppanel_file = ppanel_file->other;

	panel = savep;
}
