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

#include <sys/types.h>	/* time_t */
#include <sys/stat.h>	/* stat() */
#include <ctype.h>		/* isalnum() */
#include <errno.h>		/* errno */
#include <stdlib.h>		/* qsort() */
#include <string.h>		/* strcmp() */
#include <time.h>		/* time() */
#include <unistd.h>		/* stat() */

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

#include "clex.h"
#include "completion.h"

#include "cfg.h"		/* config_num() */
#include "control.h"	/* control_loop() */
#include "edit.h"		/* edit_update() */
#include "history.h"	/* get_hist_entry() */
#include "inout.h"		/* win_waitmsg() */
#include "list.h"		/* stat2type() */
#include "sdstring.h"	/* SDSTR() */
#include "userdata.h"	/* username_find() */
#include "ustring.h"	/* USTR() */
#include "util.h"		/* emalloc() */

/* internal use only */
#define COMPL_TYPE_PATHCMD	40	/* search $PATH */
#define COMPL_TYPE_USERDIR	50	/* with trailing slash (~name/) */

/* environment completion */
extern char **environ;
static SDSTRING *env_var;	/* environment variable names */
static int env_cnt = 0;		/* number of environment variables */

/*
 * commands are stored in several linked lists depending on
 * the first character. It is more effective than sorting.
 */
#define LIST_NR(ch) ((ch) >= 'a' && (ch) <= 'z' ? (ch) - 'a' : 26)
#define LISTS		27
typedef struct cmd {
	SDSTRING cmd;			/* command name */
	struct cmd *next;		/* linked list ptr */
} CMD;
/* PATHDIR holds info about commands in a $PATH directory */
typedef struct {
	const char *dir;		/* PATH directory name */
	time_t timestamp;		/* time of last successfull directory
							   scan, or 0 */
	dev_t device;			/* device/inode from stat() */
	ino_t inode;
	CMD *commands[LISTS];	/* lists of commands in this directory */
} PATHDIR;

static PATHDIR *pd_list;	/* list of PATHDIRs */
static int pd_cnt = 0;		/* number od PATHDIRs in pd_list */

/* input: completion request data */
static struct {
	CODE type;			/* type of completion - one of COMPL_TYPE_XXX */
	const char *str;	/* string to be completed */
	size_t strlen;		/* length of 'str' */
	const char *dir;	/* directory to attempt the completion in */
	size_t dirlen;		/* length of 'dir' */
} rq;
/* output: completion results */
static struct {
	FLAG filenames;		/* names are filenames */
	FLAG quote;			/* quote names when inserting */
	int cnt;			/* number of completion candidates */
	int err;			/* errno value (if cnt is zero) */
	size_t clen;		/* how many characters following 'str'
						   are the same for all candidates */
	char first_char[256];	/* map: first characters following 'str'*/
} compl;
/* output: candidates */
#define CC_LIST	(panel_compl.candidate)
static int cc_alloc = 0;	/* max number of candidates in CC_LIST */
static int cc_max;			/* max number of candidates to present */

static FLAG done;			/* done: completion not possible or
							  full (not partial) name was completed */

extern int errno;

static void
environ_completion_init(void)
{
	int i;
	const char *src;
	size_t len;

	for (env_cnt = 0; environ[env_cnt];)
		env_cnt++;
	if (env_cnt == 0)
		return;	/* no environment ?! */
	env_var = emalloc(env_cnt * sizeof(SDSTRING));

	for (i = 0; i < env_cnt; i++) {
		src = environ[i];
		for (len = 0; src[len] != '=' && src[len] != '\0';)
			len++;
		SD_INIT(env_var[i]);
		sd_copyn(&env_var[i],src,len);
	}
}

static void
command_completion_init(void)
{
	char *path, *p;
	int i, list;

	if ( (path = getenv("PATH")) == 0)
		return;

	/* split PATH to components */
	for (pd_cnt = 1, p = path = estrdup(path); *p; p++)
		if (*p == ':') {
			*p = '\0';
			pd_cnt++;
		}
	pd_list = emalloc(pd_cnt * sizeof(PATHDIR));

	for (i = 0, p = path; i < pd_cnt; i++) {
		pd_list[i].dir = *p ? p : ".";
		pd_list[i].timestamp = 0;
		for (list = 0; list < LISTS; list++)
			pd_list[i].commands[list] = 0;
		while (*p++)
			;
	}
}

void
completion_initialize(void)
{
	command_completion_init();
	environ_completion_init();
	completion_reconfig();
}

void
completion_reconfig(void)
{
	int i;

	if (cc_alloc > 0) {
		for (i = 0; i < cc_alloc; i++)
			sd_reset(&CC_LIST[i].str);
		free(CC_LIST);
	}

	cc_max = cc_alloc = config_num(CFG_C_SIZE);
	if (cc_max == 0)	/* C_PANEL_SIZE = AUTO */
		cc_alloc = 100;	/* enough for every screen */
	CC_LIST = emalloc(cc_alloc * sizeof(COMPL_ENTRY));
	for (i = 0; i < cc_alloc; i++)
		SD_INIT(CC_LIST[i].str);
}

static const char *
code2string(int type, int uppercase)
{
	int i;
	const char *uc;
	static char lc[24];

	switch (type) {
	case COMPL_TYPE_FILE:
		uc = "FILENAME";
		break;
	case COMPL_TYPE_DIR:
		uc = "DIRECTORY NAME";
		break;
	case COMPL_TYPE_PATHCMD:
	case COMPL_TYPE_CMD:
	case COMPL_TYPE_HIST:
		uc = "COMMAND";
		break;
	case COMPL_TYPE_USER:
	case COMPL_TYPE_USERDIR:
		uc = "USER NAME";
		break;
	case COMPL_TYPE_ENV:
		uc = "ENVIRONMENT VARIABLE";
		break;
	default:
		return "";
	}
	if (uppercase)
		return uc;

	for (i = 0; uc[i]; i++)
		lc[i] = tolower((unsigned char)uc[i]);
	lc[i] = '\0';
	return lc;
}

static int
qcmp1(const void *e1, const void *e2)
{
	return STRCOLL(
	  SDSTR(((COMPL_ENTRY *)e1)->str),
	  SDSTR(((COMPL_ENTRY *)e2)->str));
}

static int
qcmp2(const void *e1, const void *e2)
{
	return strcmp(
	  SDSTR(((COMPL_ENTRY *)e1)->str),
	  SDSTR(((COMPL_ENTRY *)e2)->str));
}

void
compl_prepare(void)
{
	panel_compl.pd->cnt = compl.cnt;
	panel_compl.pd->top = panel_compl.pd->min;
	panel_compl.pd->curs = rq.type == COMPL_TYPE_HIST ? 0 : panel_compl.pd->min;
	panel_compl.filenames = compl.filenames;
	panel_compl.description = code2string(rq.type,1);

	if (rq.type != COMPL_TYPE_HIST)
		qsort(CC_LIST,compl.cnt,sizeof(COMPL_ENTRY),
		  config_num(CFG_COLLATION) ? qcmp1 : qcmp2);
	else
		win_remark("commands are shown in order of their execution (recent first)");

	panel = panel_compl.pd;
	/* textline inherited from previous mode */
}

static void
register_candidate(const char *cand, int is_link, int file_type,
  const char *aux)
{
	size_t i;
	static const char *cand0;

	if (rq.type == COMPL_TYPE_PATHCMD && IS_FT_EXEC(file_type))
		/* check for duplicates like awk in both /bin and /usr/bin */
		for (i = 0; i < compl.cnt && i < cc_max; i++)
			if (strcmp(SDSTR(CC_LIST[i].str),cand) == 0 &&
			  IS_FT_EXEC(CC_LIST[i].file_type))
				return;

	if (compl.cnt < cc_max) {
		sd_copy(&CC_LIST[compl.cnt].str,cand);
		CC_LIST[compl.cnt].is_link   = is_link;
		CC_LIST[compl.cnt].file_type = file_type;
		CC_LIST[compl.cnt].aux       = aux;
	}

	compl.first_char[cand[rq.strlen] & 0xFF] = 1;

	if (compl.cnt == 0) {
		cand0 = SDSTR(CC_LIST[0].str); /* cand0 = cand; is wrong */
		compl.clen = strlen(cand0) - rq.strlen;
	}
	else
		for (i = 0; i < compl.clen ; i++)
			if (cand[rq.strlen + i] != cand0[rq.strlen + i]) {
				compl.clen = i;
				break;
			}

	compl.cnt++;
}

static void
complete_environ(void)
{
	int i;

	for (i = 0; i < env_cnt; i++)
		if (rq.strlen == 0
		  || strncmp(SDSTR(env_var[i]),rq.str,rq.strlen) == 0)
			register_candidate(SDSTR(env_var[i]),0,0,environ[i]);
}

static void
complete_history()
{
	int i;
	const HIST_ENTRY *ph;

	for (i = 0; (ph = get_history_entry(i)); i++)
		if (strncmp(USTR(ph->cmd),rq.str,rq.strlen) == 0)
			register_candidate(USTR(ph->cmd),0,0,
			  ph->failed ? "this command failed last time" : 0);
}

static void
complete_username(void)
{
	const char *login, *fullname;

	username_find_init(rq.str,rq.strlen);
	while ( (login = username_find(&fullname)) )
		register_candidate(login,0,0,fullname);
}

static void
complete_file(void)
{
	FLAG is_link;
	CODE type;
	const char *file, *path;
	struct stat st;
	struct dirent *direntry;
	DIR *dd;

	if ( (dd = opendir(rq.dir)) == 0) {
		compl.err = errno;
		return;
	}

	win_waitmsg();
	pathname_set_directory(rq.dir);
	while ( ( direntry = readdir(dd)) ) {
		file = direntry->d_name;
		if (rq.strlen == 0) {
			if (file[0] == '.') {
				if (file[1] == '\0')
					continue;
				if (file[1] == '.' && file[2] == '\0')
					continue;
			}
		}
		else if (strncmp(file,rq.str,rq.strlen))
			continue;

		if (lstat(path = pathname_join(file),&st) < 0)
			continue;		/* file just deleted ? */
		if ( (is_link = S_ISLNK(st.st_mode)) && stat(path,&st) < 0)
			type = FT_NA;
		else
			type = stat2type(st.st_mode,st.st_uid);

		if (rq.type == COMPL_TYPE_DIR && !IS_FT_DIR(type))
			continue;		/* must be a directory */
		if (rq.type == COMPL_TYPE_CMD
		  && !IS_FT_DIR(type) && !IS_FT_EXEC(type))
			continue;		/* must be a directory or executable */
		if (rq.type == COMPL_TYPE_PATHCMD && !IS_FT_EXEC(type))
			continue;		/* must be an executable */

		register_candidate(file,is_link,type,
		  rq.type == COMPL_TYPE_PATHCMD ? rq.dir : 0);
	}
	closedir(dd);
}

static void
pathcmd_refresh(PATHDIR *ppd)
{
	FLAG stat_ok;
	int list;
	const char *file;
	struct dirent *direntry;
	struct stat st;
	DIR *dd;
	CMD *pc;

	/*
	 * fstat(dirfd()) instead of stat() followed by opendir()
	 * would be a cleaner solution, but dirfd() is an extension
	 * not available on some systems
	 */

	stat_ok = stat(ppd->dir,&st) == 0;
	if (stat_ok && st.st_mtime < ppd->timestamp
	  && st.st_dev == ppd->device && st.st_ino == ppd->inode)
		return;

	/* clear all command lists */
	for (list = 0; list < LISTS; list++) {
		while ( (pc = ppd->commands[list]) ) {
			ppd->commands[list] = pc->next;
			sd_reset(&pc->cmd);
			free(pc);
		}
	}

	ppd->timestamp = time(0);
	if (!stat_ok || (dd = opendir(ppd->dir)) == 0) {
		ppd->timestamp = 0;
		return;
	}
	ppd->device = st.st_dev;
	ppd->inode  = st.st_ino;

	win_waitmsg();
	while ( (direntry = readdir(dd)) ) {
		file = direntry->d_name;
		list = LIST_NR(*file);
		pc = emalloc(sizeof(CMD));
		SD_INIT(pc->cmd);
		sd_copy(&pc->cmd,file);
		pc->next = ppd->commands[list];
		ppd->commands[list] = pc;
	}
	closedir(dd);
}

static void
complete_pathcmd(void)
{
	FLAG is_link;
	CODE file_type;
	int i, list;
	const char *file, *path;
	CMD *pc;
	PATHDIR *ppd;
	struct stat st;

	/* include subdirectories of the current directory */
	rq.type = COMPL_TYPE_DIR;
	complete_file();
	rq.type = COMPL_TYPE_PATHCMD;

	list = LIST_NR(*rq.str);
	for (i = 0; i < pd_cnt; i++) {
		ppd = &pd_list[i];
		if (*ppd->dir == '/') {
			/* absolute PATH directories are cached */
			pathcmd_refresh(ppd);
			pathname_set_directory(ppd->dir);
			for (pc = ppd->commands[list]; pc; pc = pc->next) {
				file = SDSTR(pc->cmd);
				if (strncmp(file,rq.str,rq.strlen) != 0)
					continue;
				if (lstat(path = pathname_join(file),&st) < 0)
					continue;
				if ( (is_link = S_ISLNK(st.st_mode))
				  && stat(path,&st) < 0)
					continue;
				file_type = stat2type(st.st_mode,st.st_uid);
				if (!IS_FT_EXEC(file_type))
					continue;
				register_candidate(file,is_link,file_type,ppd->dir);
			}
		}
		else {
			/* relative PATH directories are impossible to cache */
			rq.dir = ppd->dir;
			complete_file();
		}
	}
}

static void
complete_it(void)
{
	int i;
	size_t len;
	static USTRING dequote_str = { 0, 0 }, dequote_dir = { 0, 0 };

	/* C_PANEL_SIZE = AUTO */
	if (config_num(CFG_C_SIZE) == 0) {
		/*
		 * substract extra lines and leave the bottom
		 * line empty to show there is no need to scroll
		 */
		cc_max = display.panlines + panel_compl.pd->min - 1;
		LIMIT_MAX(cc_max,cc_alloc);
	}

	/* dequote 'str' + add terminating null byte */
	us_setsize(&dequote_str,rq.strlen + 1);
	len = dequote_txt(rq.str,rq.strlen,USTR(dequote_str));
	rq.str = USTR(dequote_str);
	rq.strlen = len;

	/* dequote 'dir' + add terminating null byte (not for "." and "/") */
	if (rq.dirlen != 1 || rq.dir[1] != '\0') {
		us_setsize(&dequote_dir,rq.dirlen + 1);
		len = dequote_txt(rq.dir,rq.dirlen,USTR(dequote_dir));
		rq.dir = USTR(dequote_dir);
		rq.dirlen = len;
	}

	compl.cnt = 0;
	compl.err = 0;
	compl.quote = 0;
	compl.filenames = 0;
	for (i = 0; i < 256; i++)
		compl.first_char[i] = 0;
	if (rq.type == COMPL_TYPE_ENV)
		complete_environ();
	else if (rq.type == COMPL_TYPE_USER || rq.type == COMPL_TYPE_USERDIR)
		complete_username();
	else if (rq.type == COMPL_TYPE_HIST)
		complete_history();
	else {
		compl.filenames = 1;
		if (textline == &line_cmd)
			compl.quote = 1;	/* in command line only */
		if (rq.type == COMPL_TYPE_PATHCMD)
			complete_pathcmd();
		else
			/* FILE, DIR, CMD completion */
			complete_file();
	}
}

static void
insert_candidate(int i)
{
	done = 1;
	edit_nu_insertstr(SDSTR(CC_LIST[i].str) + rq.strlen,compl.quote);

	if ((compl.filenames && IS_FT_DIR(CC_LIST[i].file_type))
	  || rq.type == COMPL_TYPE_USERDIR /* ~user is a directory */ ) {
		edit_nu_insertchar('/');
		done = 0; /* a directory may have subdirectories */
	}
	else if (rq.type == COMPL_TYPE_HIST)
		/* leave it as it is */;
	else if (USTR(textline->line)[textline->curs] == ' ')
		textline->curs++;
	else
		edit_nu_insertchar(' ');
	edit_update();
}

static void
show_results(void)
{
	static SDSTRING common = { 0, "" };
	const char *errmsg;

	if (compl.cnt == 0) {
		done = 1;
		switch (compl.err) {
		case 0:
			errmsg = " (no match)";
			break;
		case EACCES:
			errmsg = " (permission denied)";
			break;
		case ENOENT:
			errmsg = " (no such directory)";
			break;
		default:
			errmsg = "";
		}
		win_remark_fmt("cannot complete that %s%s",
		  code2string(rq.type,0),errmsg);
		return;
	}

	if (compl.cnt == 1) {
		insert_candidate(0);
		return;
	}

	if (compl.clen) {
		/* insert the common part of all candidates */
		sd_copyn(&common,SDSTR(CC_LIST[0].str) + rq.strlen,compl.clen);
		edit_insertstr(SDSTR(common),compl.quote);
		/*
		 * pretend that the string to be completed already contains
		 * the chars just inserted
		 */
		rq.strlen += compl.clen;
		/* 'compl.first_char' map is no longer valid */
	}

	if (compl.cnt <= cc_max) {
		control_loop(MODE_COMPL);
		return;
	}

	/* show at least the following character */
	win_completion(compl.cnt,compl.clen ? 0 : compl.first_char);
}

/*
 * check name=/some/file syntax, name should be [A-Za-z0-9_]+
 *
 * line -> the whole line
 * peq  -> the '=' character
 */
static int
check_eq_syntax(const char *line, const char *peq)
{
	char ch;
	const char *p;

	if (peq == line || peq[-1] == ' ')
		return 0;

	for (p = peq - 1; p > line; p--) {
		ch = *p;
		if (ch == ' ')
			break;
		if (ch != '_' && (ch < 'A' || ch > 'Z')
		  && (ch < 'a' || ch > 'z') && (ch < '0' || ch > '9'))
			return 0;
	}

	return 1;
}

/*
 * check backtick (`) syntax:
 * return 1 for opening backtick and 0 for closing backtick
 * (quoting issues ignored)
 */
static int
check_bt_syntax(const char *line, const char *pbt)
{
	FLAG ok;
	const char *p;

	ok = 1;
	for (p = line; p < pbt; p++)
		if (*p == '`')
			ok = !ok;
	return ok;
}

static int
is_sep(int ch)
{
	return ch == ' ' || ch == ';' || ch == '&' || ch == '|';
}

/*
 * compl_file() attempts to complete the partial text in
 * the command line
 *
 * return value:
 *   =0 = completion process completed (successfully or not)
 *   <0 = nothing to complete (current word is an empty string):
 *       -1 = first word (usually the command)
 *       -2 = not the first word (usually one of the arguments)
 */
int
compl_file(int type)
{
	int first;		/* it is the first word of the command */
	char ch, *p, *pslash, *pstart, *pend, *plinestart, *plineend;

	done = 0;
	/*
	 * plinestart -> start of the input line
	 * plineend   -> trailing null byte of the input line
	 * pstart -> start of the string to be completed
	 * pend   -> position immediately after the last character of the string
	 * pslash -> last slash '/' in string (if any)
	 */
	plinestart = USTR(textline->line);
	plineend = plinestart + textline->size;

	if (type == COMPL_TYPE_DIRPANEL || type == COMPL_TYPE_HIST) {
		first = 1;
		pstart = plinestart;
		pend = plineend;
	}
	else {
		for (first = -1, pstart = plinestart + textline->curs; first < 0;) {
			if (pstart == plinestart)
				first = 1;	/* beginning of line */
			else if ((ch = pstart[-1]) == ' ') {
				for (p = pstart; first < 0; p--) {
					if (p == plinestart)
						first = 1;
					else if ((ch = p[-1]) != ' ')
						first = is_sep((unsigned char)ch);
				}
			}
			else if (ch == '<' || ch == '>'
			  || (ch == '=' && check_eq_syntax(plinestart,pstart - 1)))
				first = 0;	/* I/O redirection or name=value */
			else if (is_sep((unsigned char)ch))
				first = 1;	/* after a command separator */
			else if (ch == '`') {
				if (!check_bt_syntax(plinestart,pstart - 1)) {
					win_remark("cannot complete after `command`");
					done = 1;
					return 0;
				}
				first = 1;	/* inside `command` */
			}
			else
				pstart--;
		}

		for (pend = plinestart + textline->curs; pend < plineend; pend++)
			if (is_sep((unsigned char)(ch = *pend)) || ch == '<' || ch == '>')
				break;
	}

	if (pstart == pend) {
		/* nothing to complete */
		done = 1;
		return first ? -1 : -2;
	}

	for (pslash = 0, p = pend; p > pstart;)
		if (*--p == '/') {
			pslash = p;
			break;
		}

	/* move cursor to the end of the current word */
	textline->curs = pend - plinestart;
	edit_update_cursor();

	/* set rq.dir, rq.dirlen, rq.str */
	if (pslash == 0 || type == COMPL_TYPE_ENV || type == COMPL_TYPE_USER
	  || type == COMPL_TYPE_USERDIR || type == COMPL_TYPE_HIST ) {
		rq.str = pstart;
		rq.dir = ".";
		rq.dirlen = 1;
	}
	else {
		rq.str = pslash + 1;
		if (*pstart == '~') {
			*pslash = '\0';
			rq.dir = dir_tilde(pstart);
			rq.dirlen = strlen(rq.dir);
			*pslash = '/';
		}
		else if (pstart == pslash) {
			rq.dir = "/";
			rq.dirlen = 1;
		}
		else {
			rq.dir = pstart;
			rq.dirlen = pslash - pstart;
		}
	}

	if (type != COMPL_TYPE_AUTO && type != COMPL_TYPE_DIRPANEL) {
		/* completion type was set by the user */
		rq.type = type;
		if (type == COMPL_TYPE_CMD && pslash == 0)
			rq.type = COMPL_TYPE_PATHCMD;
		else if (type == COMPL_TYPE_USER && *rq.str == '~')
			rq.str++;
		else if (type == COMPL_TYPE_ENV && *rq.str == '$')
			rq.str++;
	}
	else if (pslash == 0) {
		if (*pstart == '~') {
			rq.str++;
			rq.type = COMPL_TYPE_USERDIR;
		}
		else if (type == COMPL_TYPE_DIRPANEL)
			rq.type = COMPL_TYPE_DIR;
		else if (*pstart == '$') {
			rq.str++;
			rq.type = COMPL_TYPE_ENV;
		}
		else
			rq.type = first ? COMPL_TYPE_PATHCMD : COMPL_TYPE_FILE;
	}
	else if (type == COMPL_TYPE_DIRPANEL)
		rq.type = COMPL_TYPE_DIR;
	else
		rq.type = first ? COMPL_TYPE_CMD : COMPL_TYPE_FILE;

	rq.strlen = pend - rq.str;
	complete_it();
	show_results();
	return 0;
}

void
cx_compl_complete(void)
{
	insert_candidate(panel_compl.pd->curs);
	next_mode = MODE_SPECIAL_RETURN;
}

static void
complete_type(int type)
{
	if (compl_file(type))
		win_remark("there is nothing to complete");
	if (done && get_current_mode() != MODE_FILE)
		next_mode = MODE_SPECIAL_RETURN;
}

void cx_complete_auto(void)	{ complete_type(COMPL_TYPE_AUTO);	}
void cx_complete_file(void)	{ complete_type(COMPL_TYPE_FILE);	}
void cx_complete_dir(void)	{ complete_type(COMPL_TYPE_DIR);	}
void cx_complete_cmd(void)	{ complete_type(COMPL_TYPE_CMD);	}
void cx_complete_user(void)	{ complete_type(COMPL_TYPE_USER);	}
void cx_complete_env(void)	{ complete_type(COMPL_TYPE_ENV);	}
void cx_complete_hist(void)	{ complete_type(COMPL_TYPE_HIST);	}
