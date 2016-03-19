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
 * Routines in userdata.c mostly provide access to data stored
 * in /etc/passwd and /etc/group. All required data is read
 * into memory to speed up lookups. The data returned by
 * lookup_xxx() is valid only until next data refresh, the
 * caller must copy the returned string value if necessary.
 */

#include <config.h>

#include <sys/types.h>		/* getpwent() */
#include <sys/stat.h>		/* stat() */
#include <sys/utsname.h>	/* uname() */
#include <time.h>			/* time() */
#include <grp.h>			/* getgrent() */
#include <pwd.h>			/* getpwent() */
#include <string.h>			/* strcmp() */
#include <stdio.h>			/* sprintf() */
#include <stdlib.h>			/* qsort() */
#include <unistd.h>			/* stat() */

#include "clex.h"
#include "userdata.h"

#include "control.h"		/* err_exit() */
#include "inout.h"			/* win_warning() */
#include "sdstring.h"		/* SDSTR() */
#include "ustring.h"		/* us_cat() */
#include "util.h"			/* emalloc() */

/*
 * cached user(group) records are re-read when:
 *  - /etc/passwd (/etc/group) file changes, or
 *  - the cache expires (EXPIRATION in seconds) to allow
 *    changes e.g. in NIS to get detected, or
 *  - explicitely requested
 */
#define EXPIRATION	300		/* 5 minutes */

typedef struct pwdata {
	struct pwdata *next;	/* tmp pointer */
	SDSTRING login;
	SDSTRING homedir;
	SDSTRING fullname;
	uid_t uid;
} PWDATA;

typedef struct grdata {
	struct grdata *next;	/* tmp pointer */
	SDSTRING group;
	gid_t gid;
} GRDATA;

typedef struct {
	time_t timestamp;		/* when the data was obtained, or 0 */
	dev_t device;			/* device/inode for /etc/passwd */
	ino_t inode;
	int cnt;				/* # of entries */
	PWDATA **by_name;		/* sorted by name */
	PWDATA **by_uid;		/* sorted by uid */
} USERDATA;

typedef struct {
	time_t timestamp;		/* when the data was obtained, or 0 */
	dev_t device;			/* device/inode for /etc/group */
	ino_t inode;
	int cnt;				/* # of entries */
	GRDATA **by_gid;		/* sorted by gid */
} GROUPDATA;

static USERDATA  utable = { 0,0,0,0,0,0 };
static GROUPDATA gtable = { 0,0,0,0,0 };

static time_t now;

static struct {
	const char *str;
	size_t len;
	int index;
} find;						/* used by username_find() */

static int
qcmp_name(const void *e1, const void *e2)
{
	return strcmp(
	  SDSTR((*(PWDATA **)e1)->login),
	  SDSTR((*(PWDATA **)e2)->login));
}

static int
qcmp_uid(const void *e1, const void *e2)
{
	return CMP((*(PWDATA **)e1)->uid,(*(PWDATA **)e2)->uid);
}

static void
read_utable(void)
{
	int i, cnt;
	PWDATA *ud, *list;
	struct passwd *pw;
	static FLAG err = 0;

	if (utable.cnt) {
		for (i = 0; i < utable.cnt; i++) {
			sd_reset(&utable.by_name[i]->login);
			sd_reset(&utable.by_name[i]->homedir);
			sd_reset(&utable.by_name[i]->fullname);
			free(utable.by_name[i]);
		}
		free(utable.by_name);
		free(utable.by_uid);
	}
	utable.timestamp = now;

	list = 0;
	setpwent();
	for (cnt = 0; (pw = getpwent()); cnt++) {
		ud = emalloc(sizeof(PWDATA));
		SD_INIT(ud->login);
		SD_INIT(ud->homedir);
		SD_INIT(ud->fullname);
		sd_copy(&ud->login,pw->pw_name);
		sd_copy(&ud->homedir,pw->pw_dir);
		sd_copy(&ud->fullname,pw->pw_gecos);
		ud->uid = pw->pw_uid;
		ud->next = list;
		list = ud;
	}
	endpwent();
	/*
	 * I was told using errno for error detection
     * with getpwent() is not portable :-(
	 */

	if ((utable.cnt = cnt) == 0) {
		utable.timestamp = 0;
		if (!TSET(err))
			win_warning("USER ACCOUNTS: "
			  "Cannot obtain user account data.");
		return;
	}
	if (TCLR(err))
		win_warning("USER ACCOUNTS: "
			"user account data is now available.");

	/* temporary linked list -> two sorted arrays */
	utable.by_name = emalloc(cnt * sizeof(PWDATA *));
	utable.by_uid  = emalloc(cnt * sizeof(PWDATA *));
	for (ud = list, i = 0; i < cnt; i++, ud = ud->next)
		utable.by_name[i] = utable.by_uid[i] = ud;
	qsort(utable.by_name,cnt,sizeof(PWDATA *),qcmp_name);
	qsort(utable.by_uid ,cnt,sizeof(PWDATA *),qcmp_uid );
}

static int
qcmp_gid(const void *e1, const void *e2)
{
	return CMP((*(GRDATA **)e1)->gid,(*(GRDATA **)e2)->gid);
}

static void
read_gtable(void)
{
	int i, cnt;
	GRDATA *gd, *list;
	struct group *gr;
	static FLAG err = 0;

	if (gtable.cnt) {
		for (i = 0; i < gtable.cnt; i++) {
			sd_reset(&gtable.by_gid[i]->group);
			free(gtable.by_gid[i]);
		}
		free(gtable.by_gid);
	}
	gtable.timestamp = now;

	list = 0;
	setgrent();
	for (cnt = 0; (gr = getgrent()); cnt++) {
		gd = emalloc(sizeof(GRDATA));
		SD_INIT(gd->group);
		sd_copy(&gd->group,gr->gr_name);
		gd->gid = gr->gr_gid;
		gd->next = list;
		list = gd;
	}
	endgrent();

	if ((gtable.cnt = cnt) == 0) {
		gtable.timestamp = 0;
		if (!TSET(err))
			win_warning("USER ACCOUNTS: "
			  "Cannot obtain user group data.");
		return;
	}
	if (TCLR(err))
		win_warning("USER ACCOUNTS: "
			"user group data is now available.");

	/* temporary linked list -> sorted array */
	gtable.by_gid  = emalloc(cnt * sizeof(GRDATA *));
	for (gd = list, i = 0; i < cnt; i++, gd = gd->next)
		gtable.by_gid[i] = gd;
	qsort(gtable.by_gid,cnt,sizeof(GRDATA *),qcmp_gid);
}

void
userdata_initialize(void)
{
	char buffer[16];
	static SDSTRING host = { 0, "localhost" };
	static USTRING lh = { 0,0 };
	struct passwd *pw;

#ifdef HAVE_UNAME
	char ch, *pch, *pdot;
	FLAG ip;
	struct utsname ut;

	uname(&ut);
	sd_copy(&host,ut.nodename);

	/* strip the domain part */
	for (ip = 1, pdot = 0, pch = SDSTR(host); (ch = *pch); pch++) {
		if (ch == '.') {
			if (pdot == 0)
				pdot = pch;
		}
		else if (ch < '0' || ch > '9')
			ip = 0;	/* this is a name and not an IP address */
		if (!ip && pdot != 0) {
			*pdot = '\0';
			break;
		}
	}
#endif

	if ((pw = getpwuid(getuid())) == 0) {
		sprintf(buffer,"%d",(int)getuid());
		txt_printf("USER ACCOUNTS: "
		  "Cannot find your account (UID=%s) in /etc/passwd\n",buffer);
		us_cat(&lh,"uid",buffer,"@",SDSTR(host),(char *)0);
		clex_data.shell = "/bin/sh";
		clex_data.homedir = getenv("HOME");
	}
	else {
		us_cat(&lh,pw->pw_name,"@",SDSTR(host),(char *)0);
		clex_data.shell =
		  *pw->pw_shell ? estrdup(pw->pw_shell) : "/bin/sh";
		clex_data.homedir = estrdup(pw->pw_dir);
	}
	clex_data.login_at_host = USTR(lh);
	if (clex_data.homedir == 0 || *clex_data.homedir == '\0')
		clex_data.homedir = "/";
	sd_reset(&host);
}

void
userdata_expire(void)
{
	 utable.timestamp = gtable.timestamp = 0;
}

/* returns 1 if data was re-read, 0 if unchanged */
int
userdata_refresh(void)
{
	FLAG stat_ok;
	int reloaded;
	struct stat st;

	reloaded = 0;

	now = time(0);
	stat_ok = stat("/etc/passwd",&st) == 0;
	if (!stat_ok || st.st_mtime >= utable.timestamp
	  || st.st_dev != utable.device || st.st_ino != utable.inode
	  || now > utable.timestamp + EXPIRATION ) {
		read_utable();
		utable.device = stat_ok ? st.st_dev : 0;
		utable.inode  = stat_ok ? st.st_ino : 0;
		reloaded = 1;
	}

	stat_ok = stat("/etc/group",&st) == 0; 
	if (!stat_ok || st.st_mtime >= gtable.timestamp
	  || st.st_dev != gtable.device || st.st_ino != gtable.inode
      || now > gtable.timestamp + EXPIRATION) {
		read_gtable();
		gtable.device = stat_ok ? st.st_dev : 0;
		gtable.inode  = stat_ok ? st.st_ino : 0;
		reloaded = 1;
	}

	return reloaded;
}

/* simple binary search algorithm */
#define BIN_SEARCH(COUNT,CMPFUNC,RETVAL) \
	{ \
		int min, med, max, cmp; \
		for (min = 0, max = COUNT - 1; min <= max; ) { \
			med = (min + max) / 2; \
			cmp = CMPFUNC; \
			if (cmp == 0) \
				return RETVAL; \
			if (cmp < 0) \
				max = med - 1; \
			else \
				min = med + 1; \
		} \
		return 0; \
	}
/* end of BIN_SEARCH() macro */

/* numeric uid -> login name */
const char *
lookup_login(uid_t uid)
{
	BIN_SEARCH(utable.cnt,
	  CMP(uid,utable.by_uid[med]->uid),
	  SDSTR(utable.by_uid[med]->login))
}

/* numeric gid -> group name */
const char *
lookup_group(gid_t gid)
{
	BIN_SEARCH(gtable.cnt,
	  CMP(gid,gtable.by_gid[med]->gid),
	  SDSTR(gtable.by_gid[med]->group))
}

static const char *
lookup_homedir(const char *user, size_t len)
{
	if (len == 0)
		return clex_data.homedir;

	BIN_SEARCH(utable.cnt,
	  strncmp(user,SDSTR(utable.by_name[med]->login),len),
	  SDSTR(utable.by_name[med]->homedir))
}

/*
 * dir_tilde() function performs tilde substitution. It understands
 * ~user/dir notation and transforms it to proper directory name.
 * The result of the substitution (if performed) is stored in
 * a static buffer that might get overwritten by successive calls.
 */
const char
*dir_tilde(const char *dir)
{
	char ch;
	size_t i;
	const char *home;
	static USTRING buff = { 0,0 };

	if (dir[0] != '~')
		return dir;

	for (i = 1; (ch = dir[i]) && ch != '/'; i++)
		;
	home = lookup_homedir(dir + 1,i - 1);
	if (home == 0)
		return dir;		/* no such user */

	us_cat(&buff,home,dir + i,(char *)0);
	return USTR(buff);
}

/*
 * Following two functions implement username completion. First
 * username_find_init() is called to initialize the search, thereafter
 * each call to username_find() returns one matching entry in
 * alphabetical order.
 */
void
username_find_init(const char *str, size_t len)
{
	int min, med, max, cmp;

	find.str = str;
	find.len = len;

	if (len == 0) {
		find.index = 0;
		return;
	}

	for (min = 0, max = utable.cnt - 1; min <= max; ) {
		med = (min + max) / 2;
		cmp = strncmp(str,SDSTR(utable.by_name[med]->login),len);
		if (cmp == 0) {
			/*
			 * the binary search algorithm is slightly altered here,
			 * there might be more matches than one and we need to
			 * find the first one.
			 */
			if (min == max) {
				find.index = med;
				return;
			}
			max = med;
		}
		else if (cmp < 0)
			max = med - 1;
		else
			min = med + 1;
	}

	find.index = utable.cnt;
}

const char *
username_find(const char **pfullname)
{
	const char *login, *fullname;

	if (find.index >= utable.cnt)
		return 0;
	login = SDSTR(utable.by_name[find.index]->login);
	if (find.len && strncmp(find.str,login,find.len))
		return 0;
	if (pfullname) {
		fullname = SDSTR(utable.by_name[find.index]->fullname);
		if (*fullname == '\0')
			fullname = login;
		*pfullname = fullname;
	}
	find.index++;
	return login;
}

void
user_panel(void)
{
	int i, j;
	const char *filter, *login, *gecos;
	uid_t curs;

	if (utable.cnt > panel_user.usr_alloc) {
		panel_user.usr_alloc = utable.cnt;
		if (panel_user.users)
			free(panel_user.users);
		panel_user.users = emalloc(panel_user.usr_alloc * sizeof(USER_ENTRY));
	}

	curs = VALID_CURSOR(panel_user.pd) ?
	  panel_user.users[panel_user.pd->curs].uid : 0;
	filter = panel_user.pd->filtering ? panel_user.pd->filter->line : 0;

	for (i = j = 0; i < utable.cnt; i++) {
		if (curs == utable.by_uid[i]->uid)
			panel_user.pd->curs = j;
		login = SDSTR(utable.by_uid[i]->login);
		gecos = SDSTR(utable.by_uid[i]->fullname);
		if (filter && !substring(login,filter,0) && !substring(gecos,filter,1))
			continue;
		panel_user.users[j].uid = utable.by_uid[i]->uid;
		panel_user.users[j].login = login;
		panel_user.users[j++].gecos = gecos;
	}

	panel_user.pd->cnt = j;
}

void
user_prepare(void)
{
	panel_user.pd->filtering = 0;
	user_panel();
	panel_user.pd->top = panel_user.pd->min;
	panel_user.pd->curs = 0;
	panel = panel_user.pd;
	textline = 0;
}

void
group_panel(void)
{
	int i, j;
	const char *filter, *group;
	gid_t curs;

	if (gtable.cnt > panel_group.grp_alloc) {
		panel_group.grp_alloc = gtable.cnt;
		if (panel_group.groups)
			free(panel_group.groups);
		panel_group.groups = emalloc(panel_group.grp_alloc * sizeof(GROUP_ENTRY));
	}

	curs = VALID_CURSOR(panel_group.pd) ?
	  panel_group.groups[panel_group.pd->curs].gid : 0;
	filter = panel_group.pd->filtering ? panel_group.pd->filter->line : 0;

	for (i = j = 0; i < gtable.cnt; i++) {
		if (curs == gtable.by_gid[i]->gid)
			panel_group.pd->curs = j;
		group = SDSTR(gtable.by_gid[i]->group);
		if (filter && !substring(group,filter,0))
			continue;
		panel_group.groups[j].gid = gtable.by_gid[i]->gid;
		panel_group.groups[j++].group = group;
	}

	panel_group.pd->cnt = j;
}

void
group_prepare(void)
{
	panel_group.pd->filtering = 0;
	group_panel();
	panel_group.pd->top = panel_group.pd->min;
	panel_group.pd->curs = 0;
	panel = panel_group.pd;
	textline = 0;
}
