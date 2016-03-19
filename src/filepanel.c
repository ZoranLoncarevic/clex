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
#include <errno.h>		/* errno */
#include <string.h>		/* strcmp() */
#include <unistd.h>		/* chdir() */

#include "clex.h"
#include "filepanel.h"

#include "cfg.h"		/* config_str() */
#include "completion.h"	/* compl_file() */
#include "control.h"	/* err_exit() */
#include "directory.h"	/* filepos_save() */
#include "edit.h"		/* edit_macro() */
#include "exec.h"		/* execute_cmd() */
#include "inout.h"		/* win_panel() */
#include "list.h"		/* list_directory() */
#include "panel.h"		/* pan_adjust() */
#include "sdstring.h"	/* SDSTR() */
#include "undo.h"		/* undo_reset() */
#include "userdata.h"	/* userdata_expire() */
#include "ustring.h"	/* USTR() */
#include "util.h"		/* base_name() */

extern int errno;

void
files_initialize(void)
{
	const char *dir2;
	static INPUTLINE filter1, filter2;
	static PANEL_DESC panel_desc_1 = { 0,0,0,0,PANEL_TYPE_FILE,0,0,&filter1,0 };
	static PANEL_DESC panel_desc_2 = { 0,0,0,0,PANEL_TYPE_FILE,0,0,&filter2,0 };
	static PANEL_FILE panel_f1 =
		{ &panel_desc_1, { 0,0 }, 0,0,1,0,0 };
	static PANEL_FILE panel_f2 =
		{ &panel_desc_2, { 0,0 }, 0,0,1,0,0 };

	if (get_cwd_us(&panel_f1.dir) < 0)
		err_exit("Cannot get the name of the working directory");
	dir2 = config_str(CFG_DIR2);
	if (*dir2 != '/') {
		dir2 = clex_data.homedir;
		if (*dir2 != '/')
			dir2 = USTR(panel_f1.dir);
	}
	us_copy(&panel_f2.dir,dir2);
	panel_f1.other = &panel_f2;
	ppanel_file = panel_f2.other = &panel_f1;
}

void
files_main_prepare(void)
{
	static FLAG prepared = 0;

	/*
	 * allow only one run of files_main_prepare(), successive calls
	 * are merely an indirect result of panel exchange commands
	 */
	if (TSET(prepared))
		return;

	textline = &line_cmd;
	edit_nu_kill();

	panel = ppanel_file->pd;
	list_directory();
}

int
files_find(const char *name)
{
	int i;

	for (i = 0; i < ppanel_file->pd->cnt; i++)
		if (strcmp(SDSTR(ppanel_file->files[i]->file),name) == 0)
			return i;
	
	return -1;
}

/*
 * this is an error recovery procedure used when the current working
 * directory and its name stored in the file panel are not in sync
 */
static void
find_valid_cwd(void)
{
	char *dir, *p;

	win_warning("CHANGE DIR: cannot access panel's directory, "
	  "it will be changed.");

	ppanel_file->pd->cnt = ppanel_file->selected = 0;

	dir = USTR(ppanel_file->dir);
	for (p = dir + strlen(dir); p > dir ; p--)
		if (*p == '/') {
			*p = '\0';
			if (chdir(dir) == 0)
				return;
		}

	/* last resort */
	if (chdir("/") < 0)
		err_exit("Cannot access the root directory !");
	us_copy(&ppanel_file->dir,"/");
}

static void
changedir_error(int errcode)
{
	const char *msg;

	switch (errcode) {
	case EACCES:
		msg = "Permission denied";
		break;
	case ENOTDIR:
		msg = "Not a directory";
		break;
	case ENOENT:
		msg = "No such directory";
		break;
#ifdef ELOOP
	case ELOOP:
		msg = "Symbolic link loop";
		break;
#endif
	default:
		msg = "Cannot change directory";
	}
	win_warning_fmt("CHANGE DIR: %s.",msg);
}

/*
 * changes working directory to 'dir' and updates the absolute
 * pathname in the primary file panel accordingly
 *
 * changedir() returns 0 on success, -1 when the directory could
 * not be changed. In very rare case when multiple errors occur,
 * changedir() might change to other directory as requested. Note
 * that in such case it returns 0 (cwd changed).
 */
int
changedir(const char *dir)
{
	int line;
	FLAG parent;
	static USTRING savedir = { 0,0 };

	if (chdir(dir) < 0) {
		changedir_error(errno);
		return -1;
	}

	filepos_save();		/* leaving the old directory */
	us_copy(&savedir,USTR(ppanel_file->dir));

	if (get_cwd_us(&ppanel_file->dir) < 0) {
		/* not sure where we are -> must leave this dir */
		if (chdir(USTR(savedir)) == 0) {
			/* we are back in old cwd */
			changedir_error(0);
			return -1;
		}
		find_valid_cwd();
		parent = 0;	/* we have ignored the 'dir' */
	}
	else
		parent = strcmp(dir,"..") == 0;

	if (strcmp(USTR(savedir),USTR(ppanel_file->dir)))
		/* panel contents is invalid in different directory */
		ppanel_file->pd->cnt = ppanel_file->selected = 0;
	list_directory();
	/* if 'dir' parameter is a pointer to some filepanel entry */
	/* list_directory() has just invalidated it */

	/*
	 * special case: set cursor to the directory we have just left
	 * because users prefer it this way
	 */
	if (parent) {
		line = files_find(base_name(USTR(savedir)));
		if (line >= 0) {
			ppanel_file->pd->curs = line;
			pan_adjust(ppanel_file->pd);
		}
	}

	return 0;
}

/* change working directory  */
void
cx_files_cd(void)
{
	FILE_ENTRY *pfe;

	pfe = ppanel_file->files[ppanel_file->pd->curs];
	if (IS_FT_DIR(pfe->file_type)) {
		if (changedir(SDSTR(pfe->file)) == 0) {
			win_heading();
			win_panel();
		}
	}
	else
		win_remark("not a directory");
}

void cx_files_cd_root(void)
{
	changedir("/");
	win_heading();
	win_panel();
}

void cx_files_cd_parent(void)
{
	changedir("..");
	win_heading();
	win_panel();
}

void cx_files_cd_home(void)
{
	changedir(clex_data.homedir);
	win_heading();
	win_panel();
}

void
cx_files_reread(void)
{
	list_directory();
	win_panel();
}

/* reread also user account information (users/groups) */
void
cx_files_reread_ug(void)
{
	userdata_expire();
	list_directory();
	win_panel();
}

/* exchange panels */
void
cx_files_exchange(void)
{
	panel = (ppanel_file = ppanel_file->other)->pd;

	if (chdir(USTR(ppanel_file->dir)) == -1) {
		find_valid_cwd();
		list_directory();
	}
	else if (ppanel_file->expired)
		list_directory();
		/* list_directory() invokes filepos_save() */
	else
		/* put the new cwd to the top of the list */
		filepos_save();

	/* allow control_loop() to detect the 'panel' change */
	next_mode = MODE_FILE;
}

/* pressed <ENTER> - several functions: exec, chdir and insert */
void
cx_files_enter(void)
{
	FILE_ENTRY *pfe;

	if (textline->size) {
		if (execute_cmd(USTR(textline->line))) {
			cx_edit_kill();
			undo_reset();
		}
	}
	else if (ppanel_file->pd->cnt) {
		pfe = ppanel_file->files[ppanel_file->pd->curs];
		if (IS_FT_DIR(pfe->file_type)) {
			/* now doing cx_files_cd(); */
			if (changedir(SDSTR(pfe->file)) == 0) {
				win_heading();
				win_panel();
			}
		}
		else if (IS_FT_EXEC(pfe->file_type))
			edit_macro("./$F ");
	}
}

/* pressed <TAB> - also multiple functions: complete and insert */
void
cx_files_tab(void)
{
	int compl, file_type;

	/* try completion first, it returns 0 on success */
	compl = compl_file(COMPL_TYPE_AUTO);

	if (compl == -1) {
		file_type = ppanel_file->pd->cnt ?
		  ppanel_file->files[ppanel_file->pd->curs]->file_type : FT_NA;
		/* -1: nothing to complete, this will be the first word */
		if (IS_FT_EXEC(file_type))
			edit_macro("./$F ");
		else if (IS_FT_DIR(file_type))
			edit_macro("$F/");
		else
			/* absolutely clueless ! */
			win_remark("COMPLETION: please type at least the "
			  "first character");
	} else if (compl == -2)
		/* -2: nothing to complete, but not in the first word */
			edit_macro("$F ");
}
