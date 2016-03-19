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

#include <sys/types.h>		/* clex.h */
#include <ctype.h>			/* iscntrl() */
#ifdef HAVE_NCURSES_H
# include <ncurses.h>		/* KEY_xxx */
#else
# include <curses.h>
#endif
#include <stdarg.h>			/* va_list */
#include <stdlib.h>			/* exit() */

#include "clex.h"
#include "control.h"

#include "bookmarks.h"		/* bm_list_prepare() */
#include "cfg.h"			/* config_prepare() */
#include "completion.h"		/* compl_prepare() */
#include "directory.h"		/* dir_main_prepare() */
#include "edit.h"			/* cx_edit_xxx() */
#include "filepanel.h"		/* cx_files_xxx() */
#include "filter.h"			/* cx_filteredit_xxx() */
#include "help.h"			/* help_prepare() */
#include "history.h"		/* history_prepare() */
#include "inout.h"			/* win_panel() */
#include "panel.h"			/* cx_pan_xxx() */
#include "select.h"			/* select_prepare() */
#include "sort.h"			/* sort_prepare() */
#include "tty.h"			/* tty_reset() */
#include "undo.h"			/* undo_reset() */
#include "userdata.h"		/* user_prepare() */
#include "xterm_title.h"	/* xterm_title_restore() */

/*
 * This is the main control section. It is table driven.
 *
 * PANEL is the main part of the screen, it shows various data
 *   depending on the panel type, the user can scroll through it.
 *
 * TEXTLINE is a line of text where the user can enter and edit
 *   his/her input.
 *
 * KEY_BINDING contains a keystroke and a corresponding function
 *   to be called every time that key is pressed. All such handler
 *   function names begin with the cx_ prefix.
 *
 * CLEX operation mode is defined by a PANEL, TEXTLINE, and a set of
 *   KEY_BINDING tables. The PANEL and TEXTLINE are initialized by
 *   a so-called preparation function after each mode change.
 *
 * Operation mode can be changed in one of two ways:
 *   - straightforward transition from mode A to mode B; this
 *     achieved by setting the 'next_mode' global variable
 *   - nesting of modes; this is achieved by calling another
 *     instance of 'control_loop()'. To go back the variable
 *     'next_mode' must be set to MODE_SPECIAL_RETURN.
 */

typedef struct {
	FLAG escp;			/* press escape key first */
	int key;			/* if this key was pressed ... */
	void (*fn)(void);	/* ... then this function is to be invoked */
	int options;		/* options - see OPT_XXX below */
} KEY_BINDING;

#define OPT_CURS	1	/* call the handler function only if the cursor
						   is on a regular line, i.e. not on an extra line
						   or in an empty panel */
/*
 * notes: extra panel lines:   curs < 0
 *        regular panel lines: curs >= 0
 * OPT_CURS does not apply to <enter> (i.e. ctrl-M) because
 * the EXTRA_LINE structure overrides the control
 */

#define CXM(X,M) void cx_mode_ ## X (void) { control_loop(MODE_ ## M); }
static CXM(bm_list,BM_LIST)
static CXM(cfg,CFG)
static CXM(compare,COMPARE)
static CXM(deselect,DESELECT)
static CXM(dir,DIR)
static CXM(group,GROUP)
static CXM(history,HIST)
static CXM(help,HELP)
static CXM(mainmenu,MAINMENU)
static CXM(paste,PASTE)
static CXM(select,SELECT)
static CXM(sort,SORT)
static CXM(user,USER)

#define CXT(X,M) void cx_trans_ ## X (void) { next_mode = MODE_ ## M; }
static CXT(bm_list,BM_LIST)
static CXT(quit,SPECIAL_QUIT)
static CXT(return,SPECIAL_RETURN)

static void noop(void) { ; }

/* defined below */
static void menu_prepare(void);
static void cx_menu_pick(void);
static void paste_prepare(void);
static void cx_paste_pick(void);
static void cx_menu_na(void);

static void cx_tmp_w_left(void) /* transition !! */
{ win_remark("please use the alt-B key for this function"); cx_edit_w_left_(); }
static void cx_tmp_w_del(void) /* transition !! */
{ win_remark("please use the alt-D key for this function"); cx_edit_w_del_(); }
static void cx_tmp_w_right(void) /* transition !! */
{ win_remark("please use the alt-F key for this function"); cx_edit_w_right(); }
static void cx_tmp_delend(void) /* transition !! */
{ win_remark("please use the ctrl-K key for this function"); cx_edit_delend(); }

static KEY_BINDING tab_bm_lst[] = {
	{ 0,  CH_CTRL('M'),	cx_bm_list_enter,	0	},
	{ 0,  0,			0,					0	}
};

static KEY_BINDING tab_bm_mng[] = {
	{ 0,  CH_CTRL('C'),	cx_bm_mng_save,		0			},
	{ 0,  CH_CTRL('M'),	cx_bm_mng_edit,		0			},
	{ 0,  'd',			cx_bm_mng_down,		OPT_CURS	},
	{ 0,  'i',			cx_bm_mng_new,		0			},
	{ 0,  KEY_IC,		cx_bm_mng_new,		0			},
	{ 0,  'u',			cx_bm_mng_up,		OPT_CURS	},
	{ 0,  'r',			cx_bm_mng_del,		OPT_CURS	},
	{ 0,  '\177',		cx_bm_mng_del,		OPT_CURS	},
	{ 0,  CH_CTRL('H'),	cx_bm_mng_del,		OPT_CURS	},
	{ 0,  0,			0,					0			}
};

static KEY_BINDING tab_bm_edit[] = {
	{ 0,  CH_CTRL('I'),	cx_bm_edit_compl,	0 	},
	{ 0,  CH_CTRL('M'),	cx_bm_edit_enter,	0 	},
	{ 0,  0,			0,					0	}
};

static KEY_BINDING tab_cfg[] = {
	{ 0,  CH_CTRL('M'),	cx_config_enter,	0			},
	{ 0,  's',			cx_config_default,	OPT_CURS	},
	{ 0,  'o',			cx_config_original,	OPT_CURS	},
	{ 0,  0,			0,					0			}
};

static KEY_BINDING tab_cfg_edit_num[] = {
	{ 0,  CH_CTRL('M'),	cx_config_num_enter,	0	},
	{ 0,  0,			0,						0	}
};

static KEY_BINDING tab_cfg_edit_str[] = {
	{ 0,  CH_CTRL('M'),	cx_config_str_enter,	0	},
	{ 0,  0,			0,						0	}
};

static KEY_BINDING tab_common[] = {
	{ 0,  CH_CTRL('C'),	cx_trans_return,	0	},
	{ 1,  'q',			cx_trans_quit,		0	},
	{ 1,  'v',			cx_version,			0	},
	{ 0,  CH_CTRL('F'),	cx_filter,			0	},
	{ 1,  'm',			cx_menu_na,			0	},
	{ 0,  KEY_F(16),	cx_menu_na,			0	},
	{ 0,  KEY_F(1),		cx_mode_help,		0	},
	{ 0,  0,			0,					0	}
};

static KEY_BINDING tab_compare[] = {
/*
 * these lines correspond with the menu of the compare panel
 * if you change this, you must update initialization
 * in start.c and descriptions in inout.c
 */
	{ 0,  '0',			cx_compare,	0	},
	{ 0,  '1',			cx_compare,	0	},
	{ 0,  '2',			cx_compare,	0	},
	{ 0,  '3',			cx_compare,	0	},
	{ 0,  '4',			cx_compare,	0	},
	{ 0,  '5',			cx_compare,	0	},
/* compare menu ends here */
	{ 0,  CH_CTRL('M'),	cx_compare,	0	},
	{ 0,  0,			0,			0	}
};

static KEY_BINDING tab_compl[] = {
	{ 0,  CH_CTRL('I'),	cx_compl_complete,	OPT_CURS	},
	{ 0,  CH_CTRL('M'),	cx_compl_complete,	OPT_CURS	},
	{ 0,  0,			0,					0			}
};

static KEY_BINDING tab_dir[] = {
	{ 1,  'k',			cx_trans_bm_list,	0	},
	{ 0,  CH_CTRL('I'),	cx_dir_tab,			0	},
	{ 0,  CH_CTRL('M'),	cx_dir_enter,		0	},
	{ 0,  0,			0,					0	}
};

static KEY_BINDING tab_edit[] = {
	{ 1,  'b',			cx_edit_w_left,		0	},
#ifdef KEY_SLEFT
	{ 0,  KEY_SLEFT,	cx_edit_w_left,		0	},
#endif
	{ 1,  'd',			cx_edit_w_del,		0	},
	{ 1,  'f',			cx_edit_w_right,	0	},
#ifdef KEY_SRIGHT
	{ 0,  KEY_SRIGHT,	cx_edit_w_right,	0	},
#endif
	{ 0,  CH_CTRL('H'),	cx_edit_backsp,		0	},
	{ 0,  CH_CTRL('K'),	cx_edit_delend,		0	},
	{ 0,  CH_CTRL('U'),	cx_edit_kill,		0	},
	{ 0,  CH_CTRL('V'),	cx_edit_insert_spc,	0	},
	{ 0,  CH_CTRL('Z'),	cx_undo,			0	},
	{ 1,  CH_CTRL('Z'),	cx_redo,			0	},
	{ 0,  '\177',		cx_edit_delchar,	0	},
	{ 0,  KEY_LEFT,		cx_edit_left,		0	},
	{ 0,  KEY_RIGHT,	cx_edit_right,		0	},
	{ 0,  KEY_HOME,		cx_edit_begin,		0	},
#ifdef KEY_END
	{ 0,  KEY_END,		cx_edit_end,		0	},
#endif
	{ 1,  KEY_UP,		cx_edit_up,			0	},
	{ 1,  KEY_DOWN,		cx_edit_down,		0	},
	{ 0,  CH_CTRL('B'), cx_tmp_w_left,		0	}, /* transition !! */
	{ 0,  CH_CTRL('D'), cx_tmp_w_del,		0	}, /* transition !! */
	{ 0,  CH_CTRL('W'), cx_tmp_w_right,		0	}, /* transition !! */
	{ 0,  CH_CTRL('Y'),	cx_tmp_delend,		0	}, /* transition !! */
	{ 0,  0,			0,					0	}
};

static KEY_BINDING tab_editcmd[] = {
	{ 0,  CH_CTRL('A'),	cx_edit_fullpath,	0			},
	{ 0,  CH_CTRL('E'),	cx_edit_paste_dir,	0			},
	{ 0,  CH_CTRL('I'),	cx_files_tab,		0			},
	{ 1,  CH_CTRL('I'),	cx_mode_paste,		0			},
	{ 0,  CH_CTRL('M'),	cx_files_enter,		0			},
	{ 1,  CH_CTRL('M'),	cx_files_cd,		OPT_CURS	},
	{ 0,  CH_CTRL('N'),	cx_hist_next,		0			},
	{ 0,  CH_CTRL('O'),	cx_edit_paste_link,	OPT_CURS	},
	{ 0,  CH_CTRL('P'),	cx_hist_prev,		0			},
	{ 1,  CH_CTRL('R'),	cx_files_reread_ug,	0			},
	{ 0,  CH_CTRL('T'),	cx_select_toggle,	OPT_CURS	},
	{ 0,  CH_CTRL('X'),	cx_files_exchange,	0			},
	{ 1,  'g',			cx_mode_group,		0			},
	{ 1,  'm',			cx_mode_mainmenu,	0			},
	{ 1,  'p',			cx_complete_hist,	0			},
	{ 0,  KEY_F(16),	cx_mode_mainmenu,	0			},
	{ 0,  KEY_IC,		cx_select_toggle,	OPT_CURS	},
	{ 0,  KEY_F(2),		cx_edit_cmd_f2,		0			},
	{ 0,  KEY_F(3),		cx_edit_cmd_f3,		0			},
	{ 0,  KEY_F(4),		cx_edit_cmd_f4,		0			},
	{ 0,  KEY_F(5),		cx_edit_cmd_f5,		0			},
	{ 0,  KEY_F(6),		cx_edit_cmd_f6,		0			},
	{ 0,  KEY_F(7),		cx_edit_cmd_f7,		0			},
	{ 0,  KEY_F(8),		cx_edit_cmd_f8,		0			},
	{ 0,  KEY_F(9),		cx_edit_cmd_f9,		0			},
	{ 0,  KEY_F(10),	cx_edit_cmd_f10,	0			},
	{ 0,  KEY_F(11),	cx_edit_cmd_f11,	0			},
	{ 0,  KEY_F(12),	cx_edit_cmd_f12,	0			},
	{ 0,  0,			0,					0			}
};

static KEY_BINDING tab_filteredit[] = {
	{ 0,  CH_CTRL('H'),	cx_filteredit_backsp,	0	},
	{ 0,  CH_CTRL('U'),	cx_filteredit_kill,		0	},
	{ 0,  CH_CTRL('K'),	cx_filteredit_delend,	0	},
	{ 0,  '\177',		cx_filteredit_delchar,	0	},
	{ 0,  KEY_LEFT,		cx_filteredit_left,		0	},
	{ 0,  KEY_RIGHT,	cx_filteredit_right,		0	},
	{ 0,  KEY_HOME,		cx_filteredit_begin,		0	},
#ifdef KEY_END
	{ 0,  KEY_END,		cx_filteredit_end,		0	},
#endif
	{ 0,  0,			0,					0	}
};

static KEY_BINDING tab_help[] = {
	{ 0,  CH_CTRL('H'),	cx_help_back,		0	},
	{ 0,  CH_CTRL('M'),	cx_help_link,		0	},
	{ 0,  KEY_LEFT,		cx_help_back,		0	},
	{ 0,  KEY_RIGHT,	cx_help_link,		0	},
	{ 0,  KEY_F(1),		cx_help_contents,	0	},	/* avoid F1 recursion */
	{ 0,  0,			0,					0	}
};

static KEY_BINDING tab_hist[] = {
	{ 1,  '\177',		cx_hist_del,	OPT_CURS	},
	{ 1,  CH_CTRL('H'),	cx_hist_del,	OPT_CURS	},
	{ 0,  CH_CTRL('I'),	cx_hist_paste,	OPT_CURS	},
	{ 0,  CH_CTRL('M'),	cx_hist_enter,	OPT_CURS	},
	{ 0,  CH_CTRL('N'),	cx_pan_up,		0			},	/* redefine history next */
	{ 0,  CH_CTRL('P'),	cx_pan_down,	0			},	/* redefine history prev */
	{ 0,  0,			0				}
};

/* pseudo-table returned by do_action() */
static KEY_BINDING tab_insertchar[] = {
	{ 0, 0,	0, 0	}
};

static KEY_BINDING tab_mainmenu[] = {
/*
 * these lines correspond with the main menu panel,
 * if you change this, you must update initialization
 * in start.c and descriptions in inout.c
 *
 * Warning: the OPT_CURS option would check the panel_mainmenu,
 * this is probably not what you want !
 */
	/*
	 * no key for cx_mode_help, note the difference:
	 * <F1> in tab_common:    main menu -> help -> main menu
	 * help in tab_mainmenu:  main menu -> help -> file panel
	 */
	{ 0,  0,			cx_mode_help,		0	},
	{ 1,  'w',			cx_mode_dir,		0	},
	{ 1,  '/',			cx_files_cd_root,	0	},
	{ 1,  '.',			cx_files_cd_parent,	0	},
	{ 1,  '~',			cx_files_cd_home,	0	},
	{ 1,  'k',			cx_mode_bm_list,	0	},
	{ 1,  'h',			cx_mode_history,	0	},
	{ 1,  's',			cx_mode_sort,		0	},
	{ 0,  CH_CTRL('R'),	cx_files_reread,	0	},
	{ 1,  '=',			cx_mode_compare,	0	},
	{ 0,  0,			cx_filter_toggle,	0	},	/* key in tab_mainmenu2 */
	{ 1,  'u',			cx_mode_user,		0	},
	{ 0,  0,			cx_select_allfiles,	0	},	/* key in tab_mainmenu2 */
	{ 0,  0,			cx_select_nofiles,	0	},	/* key in tab_mainmenu2 */
	{ 1,  '+',			cx_mode_select,		0	},
	{ 1,  '-',			cx_mode_deselect,	0	},
	{ 1,  '*',			cx_select_invert,	0	},
	{ 1,  'c',			cx_mode_cfg,		0	},
	{ 1,  'v',			cx_version,			0	},
	{ 0,  0,			cx_trans_quit,		0	},	/* key in tab_common */
	{ 0,  0,			0,					0	}
};

/* main menu keys that are not to be used in the file panel */
/* this table must correspond with the panel_mainmenu as well ! */
static KEY_BINDING tab_mainmenu2[] = {
	{ 0,  0,			noop,				0	},
	{ 0,  0,			noop,				0	},
	{ 0,  0,			noop,				0	},
	{ 0,  0,			noop,				0	},
	{ 0,  0,			noop,				0	},
	{ 0,  0,			noop,				0	},
	{ 0,  0,			noop,				0	},
	{ 0,  0,			noop,				0	},
	{ 0,  0,			noop,				0	},
	{ 0,  0,			noop,				0	},
	{ 0,  CH_CTRL('F'),	cx_filter_toggle,	0	},
	{ 1,  'g',			cx_mode_group,		0	},
	{ 0,  '+',			cx_select_allfiles,	0	},
	{ 0,  '-',			cx_select_nofiles,	0	},
	{ 0,  0,			noop,				0	},
	{ 0,  0,			noop,				0	},
	{ 0,  0,			noop,				0	},
	{ 0,  0,			noop,				0	},
	{ 0,  0,			noop,				0	},
	{ 0,  0,			noop,				0	},
/* the main menu ends here, the following entry is hidden */
	{ 0,  CH_CTRL('M'),	cx_menu_pick,		0	},
	{ 0,  0,			0,					0	}
};

static KEY_BINDING tab_panel[] = {
	{ 0,  KEY_UP,		cx_pan_up,		0	},
	{ 0,  KEY_DOWN,		cx_pan_down,	0	},
	{ 0,  KEY_PPAGE,	cx_pan_pgup,	0	},
	{ 0,  KEY_NPAGE,	cx_pan_pgdown,	0	},
	{ 1,  KEY_HOME,		cx_pan_home,	0	},
#ifdef KEY_END
	{ 1,  KEY_END,		cx_pan_end,		0	},
#endif
	{ 0,  0,			0,				0	}
};

static KEY_BINDING tab_pastemenu[] = {
/*
 * these lines correspond with the paste menu panel,
 * if you change this, you must update initialization
 * in start.c and descriptions in inout.c
 */
	{ 0,  0,			cx_complete_auto,	0	},
	{ 0,  0,			cx_complete_file,	0	},	/* no key */
	{ 0,  0,			cx_complete_dir,	0	},	/* no key */
	{ 0,  0,			cx_complete_cmd,	0	},	/* no key */
	{ 0,  0,			cx_complete_user,	0	},	/* no key */
	{ 0,  0,			cx_complete_env,	0	},	/* no key */
	{ 1, 'p',			cx_complete_hist,	0	},
	{ 0,  KEY_F(2),		cx_insert_filename,	0	},
	{ 1,  KEY_F(2),		cx_insert_filenames,0	},
	{ 0,  CH_CTRL('A'),	cx_insert_fullpath,	0	},
	{ 0,  CH_CTRL('E'),	cx_insert_d2,		0	},
	{ 1,  CH_CTRL('E'),	cx_insert_d1,		0	},
	{ 0,  CH_CTRL('O'),	cx_insert_link,		0	},
/* the menu ends here, the following entry is hidden */
	{ 0,  CH_CTRL('M'),	cx_paste_pick,		0	},
	{ 0,  0,			0,					0	}
};

static KEY_BINDING tab_select[] = {
	{ 0,  CH_CTRL('M'),	cx_select_files,	0	},
	{ 0,  0,			0,					0	}
};

static KEY_BINDING tab_sort[] = {
	{ 0,  CH_CTRL('M'),	cx_sort_set,	0		},
	{ 0,  0,			0,				0		}
};

typedef struct {
	CODE mode;
	void (*prepare_fn)(void);
	KEY_BINDING *table[4];	/* up to 3 tables terminated with NULL;
							   order is sometimes important, only
							   the first KEY_BINDING for a given key
							   is followed */
} MODE_DEFINITION;

/* tab_edit and tab_common are appended automatically */
static MODE_DEFINITION mode_definition[] = {
	{ MODE_BM_EDIT, bm_edit_prepare, { tab_bm_edit,0 } },
	{ MODE_BM_LIST, bm_list_prepare, { tab_bm_lst,tab_panel,0 } },
	{ MODE_BM_MANAGER, bm_mng_prepare, { tab_bm_mng,tab_panel,0 } },
	{ MODE_CFG, config_prepare, { tab_cfg,tab_panel,0 } },
	{ MODE_CFG_EDIT_NUM, config_edit_num_prepare, { tab_cfg_edit_num,0 } },
	{ MODE_CFG_EDIT_TXT, config_edit_str_prepare, { tab_cfg_edit_str,0 } },
	{ MODE_COMPARE, compare_prepare, { tab_compare,tab_panel,0 } },
	{ MODE_COMPL, compl_prepare, { tab_compl,tab_panel,0 } },
	{ MODE_DESELECT, select_prepare, { tab_select,tab_panel,0 } },
	{ MODE_DIR, dir_main_prepare, { tab_dir,tab_panel,0 } },
	{ MODE_DIR_SPLIT, dir_split_prepare, { tab_dir,tab_panel,0 } },
	{ MODE_FILE, files_main_prepare, { tab_editcmd,tab_mainmenu,tab_panel,0 } },
	{ MODE_GROUP, group_prepare, { tab_panel,0 } },
	{ MODE_HELP, help_prepare, { tab_help,tab_panel,0 } },
	{ MODE_HIST, hist_prepare, { tab_hist,tab_panel,0 } },
	{ MODE_MAINMENU, menu_prepare, { tab_mainmenu,tab_mainmenu2,tab_panel,0 } },
	{ MODE_SELECT, select_prepare, { tab_select,tab_panel,0 } },
	{ MODE_PASTE, paste_prepare, { tab_pastemenu,tab_panel,0 } },
	{ MODE_SORT, sort_prepare, { tab_sort,tab_panel,0 } },
	{ MODE_USER, user_prepare, { tab_panel,0 } },
	{ 0, 0, { 0 } }
};

/* linked list of all control loop instances */
static struct operation_mode {
	CODE mode;
	PANEL_DESC *panel;
	TEXTLINE *textline;
	struct operation_mode *previous;
} mode_init = { 0,0,0,0 }, *clex_mode = &mode_init;

int
get_current_mode(void)
{
	return clex_mode->mode;
}

int
get_previous_mode(void)
{
	return clex_mode->previous->mode;
}

static MODE_DEFINITION *
get_modedef(int mode)
{
	MODE_DEFINITION *p;

	for (p = mode_definition; p->mode; p++)
		if (p->mode == mode)
			return p;

	err_exit("BUG: requested operation mode %d is invalid",mode);

	/* NOTREACHED */
	return 0;
}

static KEY_BINDING *
callfn(KEY_BINDING *tab, int in)
{
	PANEL_DESC *pd;

	if (tab == tab_insertchar)
		/* special case: in = key */
		(panel->filtering == 1 ? filteredit_insertchar : edit_insertchar)(in);
	else if ((tab[in].options & OPT_CURS) && ! VALID_CURSOR(panel))
		return 0;
	else {
		/* set cursor for tables that correspond with their respective panels */
		if ((tab == tab_mainmenu || tab == tab_mainmenu2)
		  && get_current_mode() == MODE_MAINMENU)
			pd = panel_mainmenu.pd;
		else if (tab == tab_pastemenu)
			pd = panel_paste.pd;
		else if (tab == tab_compare)
			pd = panel_compare.pd;
		else
			pd = 0;
		if (pd && in != pd->curs && in < pd->cnt) {
			pd->curs = in;
			pan_adjust(pd);
			win_panel_opt();
		}

		(*tab[in].fn)();
	}
	return tab;
}

static KEY_BINDING *
do_action(int key, KEY_BINDING **tables)
{
	int lower_ch, i, t1, t2, noesc_idx;
	FLAG esc;
	EXTRA_LINE *extra;
	KEY_BINDING *tab, *noesc_tab;
	static KEY_BINDING *append[3] = { tab_edit, tab_common, 0 };

	if (key == CH_ESC)
		return 0;

	if (panel->filtering == 1 && (key == CH_CTRL('M') || key == CH_CTRL('C'))) {
		if (panel->type != PANEL_TYPE_DIR || panel->filter->size == 0)
			filter_off();
		else
			panel->filtering = 2;
		return 0;
	}

	/* extra lines */
	if (key == CH_CTRL('M') && panel->min < 0 && panel->curs < 0) {
		extra = panel->extra + (panel->curs - panel->min);
		next_mode = extra->mode_next;
		if (extra->fn)
			(*extra->fn)();
		return 0;
	}

	esc = kbd_esc();
	lower_ch = IS_CHAR(key) ? tolower(key) : key;
	noesc_tab = 0;
	noesc_idx = 0;	/* to prevent compiler warning */
	for (t1 = t2 = 0; (tab = tables[t1]) || (tab = append[t2]);
	  tables[t1] ? t1++ : t2++) {
		if (tab == tab_edit) {
			if  (panel->filtering == 1)
				tab = tab_filteredit;
			else if (textline == 0)
				continue;
		}
		for (i = 0; tab[i].fn; i++)
			if (lower_ch == tab[i].key) {
				if (esc && !tab[i].escp) {
					/*
					 * an entry with 'escp' flag has higher priority,
					 * we must continue to search the tables to see
					 * if such entry for the given key exists
					 */
					if (noesc_tab == 0) {
						/* accept the first definition only */
						noesc_tab = tab;
						noesc_idx = i;
					}
				}
				else if (esc || !tab[i].escp)
					return callfn(tab,i);
			}
	}
	if (noesc_tab)
		return callfn(noesc_tab,noesc_idx);

	if ((textline || panel->filtering == 1)
	  && IS_CHAR(key) && !iscntrl(key) && !esc)
		return callfn(tab_insertchar,key);
#if 0
	win_remark_fmt("DEBUG: %o key (no function)",key);
#else
	win_remark("pressed key has no function "
	  "(F1 - help, ctrl-C - cancel)");
#endif
	return 0;
}

/*
 * main control loop for a selected mode 'mode'
 * control loops for different modes are nested whenever necessary
 */
void
control_loop(int mode)
{
	MODE_DEFINITION *modedef;
	KEY_BINDING *kb_tab;
	struct operation_mode current_mode;
	FLAG filter, nr;

	current_mode.previous = clex_mode;
	clex_mode = &current_mode;

	/* panel and textline inherited the from previous mode */
	clex_mode->panel = clex_mode->previous->panel;
	clex_mode->textline = clex_mode->previous->textline;

	for (next_mode = mode; /* until break */; ) {
		modedef = get_modedef(clex_mode->mode = next_mode);
		next_mode = 0;
		(*modedef->prepare_fn)();
		win_heading();
		if (panel != clex_mode->panel) {
			if (panel->filtering) {
				win_filter();
				filter_update();
			} else {
				if (clex_mode->panel && clex_mode->panel->filtering)
					win_filter();
				pan_adjust(panel);
				win_panel();
			}
			clex_mode->panel = panel;
		}

		if (textline != clex_mode->textline) {
			undo_reset();
			edit_adjust();
			win_edit();
			clex_mode->textline = textline;
		}

		for (; /* until break */;) {
			undo_before();
			kb_tab = do_action(kbd_input(),modedef->table);
			undo_after();
			if (next_mode) {
				if (next_mode == MODE_SPECIAL_RETURN
				  && clex_mode->previous->mode == 0) {
					win_remark("to quit CLEX press <esc> Q");
					next_mode = 0;
				}
				else
					break;
			}

			/* some special handling not implemented with tables */
			switch (clex_mode->mode) {
			case MODE_COMPL:
				if (kb_tab == tab_edit || kb_tab == tab_insertchar)
					next_mode = MODE_SPECIAL_RETURN;
				break;
			case MODE_DIR:
			case MODE_DIR_SPLIT:
				nr = kb_tab != tab_panel && textline->size > 0;
				if (panel->norev != nr) {
					panel->norev = nr;
					win_edit();
					win_panel_opt();
				}
				break;
			case MODE_MAINMENU:
				if (kb_tab == tab_mainmenu || kb_tab == tab_mainmenu2)
					next_mode = MODE_SPECIAL_RETURN;
				break;
			}
			if (panel->filtering && panel->filter->changed)
				filter_update();
			if (next_mode)
				break;
		}

		if (next_mode == MODE_SPECIAL_QUIT)
			err_exit("Normal exit");

		if (next_mode == MODE_SPECIAL_RETURN) {
			next_mode = 0;
			break;
		}
	}

	clex_mode = clex_mode->previous;
	win_heading();
	if (panel != clex_mode->panel) {
		filter = panel->filtering || clex_mode->panel->filtering;
		panel = clex_mode->panel;
		if (filter)
			win_filter();
		pan_adjust(panel);		/* screen size might have changed */
		win_panel();
	}
	if (textline != clex_mode->textline) {
		textline = clex_mode->textline;
		edit_adjust();
		win_edit();
	}
}

static void
menu_prepare(void)
{
	/* leave cursor position unchanged */
	panel = panel_mainmenu.pd;
	textline = 0;
}

static void
cx_menu_pick(void)
{
	(*tab_mainmenu[panel_mainmenu.pd->curs].fn)();
	if (next_mode == 0)
		next_mode = MODE_SPECIAL_RETURN;
}

static void
paste_prepare(void)
{
	/* leave cursor position unchanged */
	panel = panel_paste.pd;
	/* textline unchanged */
}

static void
cx_paste_pick(void)
{
	(*tab_pastemenu[panel_paste.pd->curs].fn)();
}

static void
cx_menu_na(void)
{
	win_remark("the menu (alt-M) exists only in the file panel");
}

void
cx_version(void)
{
	win_remark("Welcome to CLEX " VERSION " !");
}

/*
 * err_exit() is the only exit function that terminates CLEX main
 * process. It is used for normal (no error) termination as well.
 */
void
err_exit(const char *format, ...)
{
	va_list argptr;

	/*
	 * all cleanup functions used here:
	 *  - must not call err_exit()
	 *  - must not require initialization
	 */
	xterm_title_restore();
	if (display.curses)
		curses_stop();
	tty_reset();

	fputs("\nTerminating CLEX: ",stdout);
	va_start(argptr,format);
	vprintf(format,argptr);
	va_end(argptr);
	putchar('\n');

	tty_pgrp_reset();
	exit(0);
	/* NOTREACHED */
}
