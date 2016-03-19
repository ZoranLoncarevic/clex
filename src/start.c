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

#include <sys/types.h>	/* umask() */
#include <sys/stat.h>	/* umask() */
#include <stdio.h>		/* puts() */
#include <stdlib.h>		/* putenv() */
#include <string.h>		/* strcmp() */
#include <unistd.h>		/* getpid() */

#include "clex.h"

#include "bookmarks.h"	/* bm_initialize() */
#include "cfg.h"		/* config_initialize() */
#include "completion.h"	/* completion_initialize() */
#include "control.h"	/* control_loop() */
#include "directory.h"	/* dir_initialize() */
#include "exec.h"		/* exec_initialize() */
#include "filepanel.h"	/* files_initialize() */
#include "help.h"		/* help_initialize() */
#include "history.h"	/* hist_initialize() */
#include "inout.h"		/* curses_initialize() */
#include "lang.h"		/* lang_initialize() */
#include "list.h"		/* list_initialize() */
#include "signals.h"	/* signal_initialize() */
#include "tty.h"		/* tty_initialize() */
#include "undo.h"		/* undo_init() */
#include "userdata.h"	/* userdata_initialize() */
#include "ustring.h"	/* US_INIT() */
#include "util.h"		/* base_name() */
#include "xterm_title.h"/* xterm_title_initialize() */

/* GLOBAL VARIABLES */

static EXTRA_LINE el_leave[1] = {
	{ 0,0,MODE_SPECIAL_RETURN,0 }
};
static EXTRA_LINE el_bm_lst[3] = {
	{ 0,0,MODE_SPECIAL_RETURN,0 },
	{ "Manage bookmarks",0,MODE_BM_MANAGER,0 },
	{ "Bookmark the current working directory",
	  "@1" /* handled by win_info_extra_line() */,
	  0,cx_bm_list_bookmark }
};
static EXTRA_LINE el_bm_mng[2] = {
	{ 0, "Bookmarks are saved automatically",MODE_BM_LIST,cx_bm_mng_save },
	{ "Revert the bookmarks to previous state",
	  "All changes will be lost",0,cx_bm_mng_revert }
};
static EXTRA_LINE el_cfg[3] = {
	{ "Cancel","No configuration change",MODE_SPECIAL_RETURN,0 },
	{ "Accept","Use the new configuration in this session",
	  MODE_SPECIAL_RETURN,cx_config_save },
	{ "Accept+Save","Save the configuration to ~/.clexrc",
	  MODE_SPECIAL_RETURN,cx_config_save }
};
static EXTRA_LINE el_cfg_admin[2] = {
	{ "Exit",
	  "Exit program (don't forget to save the configuration first)",
	  MODE_SPECIAL_QUIT,0 },
	{ "Save","Save the new system-wide defaults to " CONFIG_FILE,
	  0,cx_config_admin_save }
};
/* pd_dir_split uses only the first entry of el_dir */
static EXTRA_LINE el_dir[2] = {
	{ 0,0,MODE_SPECIAL_RETURN,cx_dir_enter },
	{ "Bookmarks",0,MODE_BM_LIST,cx_dir_enter }
};
static EXTRA_LINE el_grp[2] = {
	{ 0,0,MODE_SPECIAL_RETURN,0 },
	{ "Switch to user data",0,MODE_USER,0 }
};
static EXTRA_LINE el_help[1] = {
	{ "HELP: Table of Contents",0,0,cx_help_contents }
};
static EXTRA_LINE el_usr[2] = {
	{ 0,0,MODE_SPECIAL_RETURN,0 },
	{ "Switch to group data",0,MODE_GROUP,0 }
};

static INPUTLINE il_filt;

/*
 * PANEL_DESC initialization:
 *   cnt, top, curs, min, type, norev, extra, filter, filtering
 */
static PANEL_DESC pd_bm_lst =
  { 0,0,0,-3,PANEL_TYPE_BM,0,el_bm_lst,0,0};
static PANEL_DESC pd_bm_mng =
  { 0,0,0,-2,PANEL_TYPE_BM,0,el_bm_mng,0,0};
static PANEL_DESC pd_cfg =
  { CFG_VARIABLES,0,0,-3,PANEL_TYPE_CFG,0,el_cfg,0,0 };
static PANEL_DESC pd_compare =
  /* 6 items in this menu */
  {  6,-1,2,-1,PANEL_TYPE_COMPARE,0,el_leave,0,0 };
static PANEL_DESC pd_compl =
  { 0,0,0,-1,PANEL_TYPE_COMPL,0,el_leave,0,0 };
static PANEL_DESC pd_dir =
  { 0,0,0,-2,PANEL_TYPE_DIR,0,el_dir,&il_filt,0 };
static PANEL_DESC pd_dir_split =
  { 0,0,0,-1,PANEL_TYPE_DIR_SPLIT,0,el_dir,0,0 };
static PANEL_DESC pd_grp =
  { 0,0,0,-2,PANEL_TYPE_GROUP,0,el_grp,&il_filt,0 };
static PANEL_DESC pd_help =
  { 0,0,0,-1,PANEL_TYPE_HELP,1,el_help,0,0 };
static PANEL_DESC pd_hist =
  { 0,0,0,-1,PANEL_TYPE_HIST,0,el_leave,&il_filt,0 };
static PANEL_DESC pd_mainmenu =
  /* 20 items in this menu */
  { 20,-1,-1,-1,PANEL_TYPE_MAINMENU,0,el_leave,0,0 };
static PANEL_DESC pd_paste =
  /* 13 items in this menu */
  { 13,-1,-1,-1,PANEL_TYPE_PASTE,0,el_leave,0,0 };
static PANEL_DESC pd_sort =
  /* 7 items in this menu */ 
  {  7,0,0,-1,PANEL_TYPE_SORT,0,el_leave,0,0 };
static PANEL_DESC pd_usr =
  { 0,0,0,-2,PANEL_TYPE_USER,0,el_usr,&il_filt,0 };
PANEL_DESC *panel = 0;
PANEL_BM panel_bm_lst = { &pd_bm_lst }, panel_bm_mng = { &pd_bm_mng };
PANEL_COMPL panel_compl = { &pd_compl };
PANEL_CFG panel_cfg = { &pd_cfg };
PANEL_DIR panel_dir = { &pd_dir }, panel_dir_split = { &pd_dir_split };
PANEL_GROUP panel_group = { &pd_grp,0,0 };
PANEL_HELP panel_help = { &pd_help };
PANEL_HIST panel_hist = { &pd_hist };
PANEL_MENU panel_mainmenu = { &pd_mainmenu };
PANEL_MENU panel_compare = { &pd_compare };
PANEL_MENU panel_paste = { &pd_paste };
PANEL_SORT panel_sort =  { &pd_sort, SORT_NAME };
PANEL_USER panel_user = { &pd_usr,0,0 };
PANEL_FILE *ppanel_file;
DISPLAY display = { 0,0,0,0,0,0,0 };
CLEX_DATA clex_data;
TEXTLINE *textline = 0, line_cmd, line_tmp, line_dir;
CODE next_mode;
const void *pcfg[CFG_VARIABLES];

int
main(int argc, char *argv[])
{
	int i;
	FLAG help, version;
	const char *arg;

	/* check command line arguments */
	clex_data.admin = help = version = 0;
	for (i = 1; i < argc; i++) {
		arg = argv[i];
		if (strcmp(arg,"-a") == 0 || strcmp(arg,"--admin") == 0)
			clex_data.admin = 1;
		else if (strcmp(arg,"--help") == 0)
			help = 1;
		else if (strcmp(arg,"--version") == 0)
			version = 1;
		else
			err_exit("Unrecognized option '%s'\n"
			  "    try '%s --help' for more information",
			  arg,base_name(argv[0]));
	}
	if (version)
		puts(
		  "\nCLEX File Manager " VERSION "\n"
		  "  compiled with POSIX job control:  "
#ifdef _POSIX_JOB_CONTROL
  "yes\n"
#else
  "no\n"
#endif
		  "  compiled with locale support:     "
#if defined(HAVE_SETLOCALE) && defined(HAVE_STRFTIME)
  "yes\n"
#else
  "no\n"
#endif
		  "  system-wide configuration file:   " CONFIG_FILE "\n\n"

		  "Copyright (C) 2001-2006 Vlado Potisk <vlado_potisk@clex.sk>"
		  "\n\n"
		  "This is free software distributed without any warranty.\n"
		  "See the GNU General Public License for more details.\n"
		  "\n"
		  "Project homepage is http://www.clex.sk");
	if (help)
		printf(
		  "\nUsage: %s [OPTION]\n\n"
		  "  -a, --admin      admin mode (system-wide configuration)\n"
		  "      --version    display program version and exit\n"
		  "      --help       display this help and exit\n",
		  base_name(argv[0]));
	if (help || version)
		exit(0);	/* no cleanup necessary at this stage */

	/* real start */
	puts("\n\n\n\nStarting CLEX " VERSION);

#ifdef HAVE_PUTENV
	putenv("CLEX=" VERSION);	/* let's have $CLEX (just so) */
#endif

	/* initialize program data */
	clex_data.pid = getpid();
	clex_data.umask = umask(0777);
	umask(clex_data.umask);
	clex_data.isroot = geteuid() == 0;  /* 0 or 1 */;

	US_INIT(line_tmp.line);	/* belongs to nowhere, initialize it here */
	undo_init(&line_tmp);

	signal_initialize();	/* set signal dispositions */
	userdata_initialize();	/* must be done before config */
	config_initialize();	/* read configuration asap */

	/*
	 * initialize the rest,
	 * the order is no longer important (except when noted)
	 */
	help_initialize();
	if (!clex_data.admin) {
		/* all these features are not used in admin mode */
		bm_initialize();
		completion_initialize();
		dir_initialize();
		exec_initialize();
		files_initialize();
		hist_initialize();
		lang_initialize();	/* lang_ before list_ */
		list_initialize();
		xterm_title_initialize();
	}
	else {
		/* modify the configuration panel for use in the admin mode */
		pd_cfg.min = -2;
		pd_cfg.extra = el_cfg_admin;
	}

	/* start CURSES screen handling */
	tty_initialize();
	curses_initialize();

	/* let's go ! */
	cx_version();
	control_loop(clex_data.admin ? MODE_CFG : MODE_FILE);

	/* NOTREACHED */
	return 0;
}
