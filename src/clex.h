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
 * naming convention for strings and buffers:
 *   #define XXX_STR is a buffer size, i.e. with the trailing null byte
 *   #define XXX_LEN is max string length, i.e. without the null byte
 */

/* useful macros */
#define ARRAY_SIZE(X)	(sizeof(X) / sizeof(X[0]))
#define TSET(X)			((X) ? 1 : ((X) = 1, 0))	/* test & set */
#define TCLR(X)			((X) ? ((X) = 0, 1) : 0)	/* test & clear */
#define LIMIT_MIN(X,MIN)	do if ((X) < (MIN)) (X) = (MIN); while(0)
#define LIMIT_MAX(X,MAX)	do if ((X) > (MAX)) (X) = (MAX); while(0)
#define CH_ESC			'\033'						/* ASCII escape */
#define CH_CTRL(X)		((X) & 0x1F)				/* ASCII ctrl-X */
#define IS_CHAR(X)		(((X) & 0xFF) == (X))		/* X is a char */
/* CMP: no overflow and result is an int even if V1 and V2 are not */
#define CMP(V1,V2) ((V1) == (V2) ? 0 : (V1) < (V2) ? -1 : 1)
/* you can't do this in one pass: */
#define STR(X)	STRINGIZE(X)
#define STRINGIZE(X)	#X

/* replacement function */
extern char *my_strerror(int);		/* in util.c */

/* typedefs */
typedef unsigned short int FLAG;	/* true or false */
typedef short int CODE;				/* usually some #define-d value */

/* minimal required screen size */
#define MIN_COLS	64
#define MIN_LINES	12

/*
 * operation modes:
 * value 0 is reserved, it means mode unchanged in control_loop()
 *   and also mode not set during startup
 */
#define MODE_BM_LIST			 1
#define MODE_BM_MANAGER			 2
#define MODE_BM_EDIT			 3
#define MODE_CFG				 4
#define MODE_CFG_EDIT_NUM		 5
#define MODE_CFG_EDIT_TXT		 6
#define MODE_COMPL				 7
#define MODE_COMPARE			 8
#define MODE_DESELECT			 9
#define MODE_DIR				10
#define MODE_DIR_SPLIT			11
#define MODE_FILE				12
#define MODE_GROUP				13
#define MODE_HELP				14
#define MODE_HIST				15
#define MODE_MAINMENU			16
#define MODE_PASTE				17
#define MODE_SELECT				18
#define MODE_SORT				19
#define MODE_USER				20
/* pseudo-modes */
#define MODE_SPECIAL_QUIT		98
#define MODE_SPECIAL_RETURN		99

/* see sdstring.c for details */
#define SDSTRING_LEN 15
typedef struct {
	char *SDname;					/* string if long, otherwise null */
	char SDmem[SDSTRING_LEN + 1];	/* string if short */
} SDSTRING;

/* see ustring.c for details */
typedef struct {
	char *USstr;			/* space to hold some character string */
	size_t USalloc;			/* size of the allocated memory */
} USTRING;

/* info about screen display/layout/appearance */
typedef struct {
	FLAG curses;		/* curses active */
	FLAG wait;			/* a message has been written to the
						   text-mode screen, wait for a keypress
						   before starting curses */
	int scrcols;		/* number of columns */
	int pancols;		/* number of columns in the panel area */
	int scrlines;		/* number of lines */
	int panlines;		/* number of lines in the panel area */
	int textline_area;	/* number of chars available for
						   the textline (including the prompt) */
	int date_len;		/* length of date/time field */
} DISPLAY;

typedef struct {
	const char *login_at_host;	/* my name: user@host */
	char *shell;				/* my login shell */
	const char *homedir;		/* my home directory */
	pid_t pid;					/* process ID */
	mode_t umask;				/* umask value */
	FLAG isroot;				/* effective uid is 0(root) */
	FLAG admin;					/* admin mode: clex -a */
	CODE shelltype;				/* Bourne shell (0), C-shell (1), other (2) */
} CLEX_DATA;

/* description of an editing operation */
# define OP_NONE	0	/* no change (cursor movement is OK) */
# define OP_INS		1	/* simple insert */
# define OP_DEL		2	/* simple deletion */
# define OP_CHANGE	3	/* modification other than insert or delete */
typedef struct {
	int code;		/* one of OP_XXX defined above */
/* 'pos' and 'len' are used with OP_INSERT and OP_DELETE only */
	int pos;		/* position within the edited string */
	int len;		/* length of the inserted/deleted part */
} EDIT_OP;

#define UNDO_LEVELS 10	/* undo steps */

/* line of text where the user can enter and edit his/her input */
typedef struct {
	const char *prompt;	/* prompt */
	int promptlen;		/* prompt length */
	USTRING line;		/* user's input */
	int size;			/* number of chars in the line */
	int curs;			/* cursor position from 0 to 'size' */
	int offset;			/* offset - when the line is too long,
						   first 'offset' characters are hidden */
	/* values for the UNDO function */
	struct {
		USTRING save_line;
		int save_size;
		int save_curs;
		int save_offset;
	} undo [UNDO_LEVELS];	/* used in a circular manner */
	int undo_base;			/* index of the first entry */
	int undo_levels;		/* occupied entries for undo */
	int redo_levels;		/* free entries usable for redo */
	EDIT_OP last_op;		/* last editing operation */
} TEXTLINE;

/* minimalistic version of TEXTLINE */
/* currently used only for panel filters */
#define INPUT_STR 25
typedef struct {
	char line[INPUT_STR];	/* user's input */
	int size;				/* number of chars in the line */
	int curs;				/* cursor position from 0 to 'size' */
	FLAG changed;			/* 'line' has been modified */
} INPUTLINE;

/********************************************************************/

/*
 * panel types
 * if you change this, you must also update
 * the draw_panel_line() in inout.c
 */
#define PANEL_TYPE_BM			 0
#define PANEL_TYPE_CFG			 1
#define PANEL_TYPE_COMPARE		 2
#define PANEL_TYPE_COMPL		 3
#define PANEL_TYPE_DIR			 4
#define PANEL_TYPE_DIR_SPLIT	 5
#define PANEL_TYPE_FILE			 6
#define PANEL_TYPE_GROUP		 7
#define PANEL_TYPE_HELP			 8
#define PANEL_TYPE_HIST			 9
#define PANEL_TYPE_MAINMENU		10
#define PANEL_TYPE_PASTE		11
#define PANEL_TYPE_SORT			12
#define PANEL_TYPE_USER			13
#define PANEL_TYPE_NONE			99	/* not set (only during startup) */

/*
 * extra lines appear in a panel before the real first line,
 * extra lines:  -MIN .. -1
 * real lines:      0 .. MAX
 */
typedef struct {
	const char *text;		/* text to be displayed in the panel */
							/* default (if null): "Leave this panel" */
	const char *info;		/* text to be displayed in the info line */
	/* when this extra line is selected: */
	CODE mode_next;			/* set next_mode to this mode and then ... */
	void (*fn)(void); 		/* ... invoke this function */
} EXTRA_LINE;

/* description of a panel */
typedef struct {
	int cnt, top;	/* panel lines: total count, top of the screen */
	int curs, min;	/* panel lines: cursor bar, top of the panel */
	/*
	 * 'min' is used to insert extra lines before the real first line
     * which is always line number 0; to insert N extra lines set
	 * 'min' to -N; the number of extra lines is not included in 'cnt' 
	 */
	CODE type;		/* panel type: one of PANEL_TYPE_XXX */
	FLAG norev;		/* do not show the current line in reversed video */
	EXTRA_LINE *extra;	/* extra panel lines */
	INPUTLINE *filter;	/* filter (if applicable to this panel type) */
	CODE filtering;		/* filter: 0 = off */
						/* 1 = on - focus on the filter string */
						/* 2 = on - focus on the command line */
} PANEL_DESC;

#define VALID_CURSOR(P) ((P)->cnt > 0 && (P)->curs >= 0 && (P)->curs < (P)->cnt)

/********************************************************************/

/*
 * file types recognized in the file panel,
 * if you change this, you must also update
 * the names of the types in inout.c
 */
#define FT_PLAIN_FILE		 0
#define FT_PLAIN_EXEC		 1
#define FT_PLAIN_SUID		 2
#define FT_PLAIN_SUID_ROOT	 3
#define FT_PLAIN_SGID		 4
#define FT_DIRECTORY		 5
#define FT_DIRECTORY_MNT	 6
#define FT_DEV_BLOCK		 7
#define FT_DEV_CHAR			 8
#define FT_FIFO				 9
#define FT_SOCKET			10
#define FT_OTHER			11
#define FT_NA				12

/* file type tests */
#define IS_FT_PLAIN(X)		((X) >= 0 && (X) <= 4)
#define IS_FT_EXEC(X)		((X) >= 1 && (X) <= 4)
#define IS_FT_DIR(X)		((X) >= 5 && (X) <= 6)
#define IS_FT_DEV(X)		((X) >= 7 && (X) <= 8)

/*
 * if you change any of the FE_XXX_STR #defines, you must change
 * the corresponding  stat2xxx() function in list.c accordingly
 */
/* text buffer sizes */			/* examples: */
#define FE_LINKS_STR	 4		/* 1  999  max  */
#define FE_TIME_STR		12		/* 12:34am  1.01.04  01-jan-2004 */
#define FE_SIZE_DEV_STR	12		/* 3.222.891Ki */
#define FE_MODE_STR 	 5		/* 0644 */
#define FE_NAME_STR		10		/* root */
#define FE_OWNER_STR	(2 * FE_NAME_STR)	/* root:mail */

/*
 * file description - exhausting, isn't it ?
 * we allocate many of these, bitfields save memory
 */
typedef struct {
	SDSTRING file;			/* file name */
	USTRING link;			/* where the symbolic link points to */
	const char *extension;	/* file name extension (suffix) */
	time_t mtime;			/* last file modification */
	off_t size;				/* file size */
	dev_t devnum;			/* major/minor numbers (devices only) */
	CODE file_type;			/* one of FT_XXX */
	uid_t uid, gid;			/* owner and group */
	short int mode12;		/* file mode - low 12 bits */
	unsigned int select:1;		/* flag: this entry is selected */
	unsigned int symlink:1;		/* flag: it is a symbolic link */
	unsigned int dotdir:2;		/* . (1) or .. (2) directory */
	unsigned int fmatch:1;		/* flag: matches the filter */
	/*
	 * note: the structure members below are used
	 * only when the file panel layout requires them
	 */
	unsigned int normal_mode:1;	/* file mode same as "normal" file */
	unsigned int links:1;		/* has multiple hard links */
	char atime_str[FE_TIME_STR];	/* access time */
	char ctime_str[FE_TIME_STR];	/* inode change time */
	char mtime_str[FE_TIME_STR];	/* file modification time */
	char links_str[FE_LINKS_STR];	/* number of links */
	char mode_str[FE_MODE_STR];		/* file mode - octal number */
	char owner_str[FE_OWNER_STR];	/* owner and group */
	char size_str[FE_SIZE_DEV_STR];	/* file size or dev major/minor */
} FILE_ENTRY;

typedef struct ppanel_file {
	PANEL_DESC *pd;
	USTRING dir;			/* working directory */
	struct ppanel_file *other;	/* primary <--> secondary panel ptr */
	int selected;			/* number of selected entries */
	FLAG expired;			/* expiration: panel needs re-read */
	FLAG filtype;			/* filter type: 0 = substring, 1 = pattern */
	int filt_cnt;			/* saved number of entries while filtering is on */
	int filt_sel;			/* selected entries NOT matched by the filter */
	int fe_alloc;			/* allocated FILE_ENTRies in 'files' below */
	FILE_ENTRY **files;		/* main part: list of files in panel's
							   working directory 'dir' */
} PANEL_FILE;
/*
 * filter off: 0 .. cnt-1     = all file entries
 * filter on:  0 .. cnt-1     = entries matching the filter expression
 *           cnt .. filtcnt-1 = saved entries not matching the filter
 */

/********************************************************************/

typedef struct {
	PANEL_DESC *pd;
	USTRING *bm;
} PANEL_BM;

/********************************************************************/

/*
 * file sort order - if you change this, you must also update
 * panel initization in start.c and descriptions in inout.c
 */
#define SORT_NAME		0
#define SORT_SUFFIX		1
#define SORT_SIZE		2
#define SORT_SIZE_REV	3
#define SORT_TIME		4
#define SORT_TIME_REV	5
#define SORT_EMAN		6		/* Name <--> emaN */

typedef struct {
	PANEL_DESC *pd;
	CODE order;				/* file sort order: one of SORT_XXX */
} PANEL_SORT;

#define HIDE_NEVER	0
#define HIDE_HOME	1
#define HIDE_ALWAYS	2

/********************************************************************/

typedef struct {
	const char *name;	/* directory name (see also shlen) */
	int shlen;			/* PANEL_DIR: length of the repeating name part
							 (used for fancy formatting)
						   PANEL_DIR_SPLIT: length of the string 'name'
							 which is NOT null terminated */
} DIR_ENTRY;

typedef struct {
	PANEL_DESC *pd;
	DIR_ENTRY *dir;			/* list of directories to choose from */
} PANEL_DIR, PANEL_DIR_SPLIT;

/********************************************************************/
/*
 * configuration variables,
 * if you change this, you must also update
 * the config[] array in cfg.c
 */
#define CFG_VARIABLES		37

/* appearance */
#define CFG_FRAME			 0
#define CFG_CMD_LINES		 1
#define CFG_XTERM_TITLE		 2
#define CFG_GROUP_FILES		 3
#define CFG_PROMPT			 4
#define CFG_LAYOUT1			 5
#define CFG_LAYOUT2			 6
#define CFG_LAYOUT3			 7
#define CFG_LAYOUT			 8
#define CFG_KILOBYTE		 9
#define CFG_FMT_NUMBER		10
#define CFG_FMT_TIME		11
#define CFG_FMT_DATE		12
#define CFG_COLLATION		13
/* command execution */
#define CFG_SHELLPROG		14
#define CFG_CMD_F3			15
#define CFG_CMD_F4			16
#define CFG_CMD_F5			17
#define CFG_CMD_F6			18
#define CFG_CMD_F7			19
#define CFG_CMD_F8			20
#define CFG_CMD_F9			21
#define CFG_CMD_F10			22
#define CFG_CMD_F11			23
#define CFG_CMD_F12			24
#define CFG_WARN_RM			25
#define CFG_WARN_LONG		26
#define CFG_WARN_SELECT		27
/* other */
#define CFG_DIR2			28
#define CFG_HELPFILE		29
#define CFG_QUOTE			30
#define CFG_C_SIZE			31
#define CFG_D_SIZE			32
#define CFG_H_SIZE			33
/* additional configuration */
#define CFG_VIEWER_CMD			34
#define CFG_NOPROMPT_CMDS		35
#define CFG_SHOW_HIDDEN			36

/* max string lengths */
#define CFGVAR_LEN		16	/* name */
#define CFGVALUE_LEN	60	/* string value */

typedef struct {
	const char *var;		/* name of the variable */
	const char *help;		/* one line help */
	void *table;			/* -> internal table with details */
	unsigned int isnum:1;	/* is numeric (not string) */
	unsigned int changed:1;	/* value changed */
	unsigned int saveit:1;	/* value should be saved to disk */
} CONFIG_ENTRY;

typedef struct {
	PANEL_DESC *pd;
	CONFIG_ENTRY *config;	/* list of all configuration variables */
} PANEL_CFG;

/********************************************************************/

typedef struct {
	USTRING cmd;			/* command text */
	FLAG failed;			/* command failed or not */
} HIST_ENTRY;

typedef struct {
	PANEL_DESC *pd;
	HIST_ENTRY **hist;		/* list of previously executed commands */
} PANEL_HIST;

/********************************************************************/

typedef struct {
	const char *txt;		/* help text to be displayed */
	const char *aux;		/* additional data: link or page heading */
} HELP_ENTRY;

typedef struct {
	PANEL_DESC *pd;
	CODE pagenum;			/* internal number of current page */
	const char *heading;	/* heading of the current help page */
	HELP_ENTRY *line;
} PANEL_HELP;

/********************************************************************/

typedef struct {
	SDSTRING str;			/* name suitable for a completion */
	FLAG is_link;			/* filenames only: it is a symbolic link */
	CODE file_type;			/* filenames only: one of FT_XXX */
	const char *aux;		/* additional information (info line) */
} COMPL_ENTRY;

typedef struct {
	PANEL_DESC *pd;
	FLAG filenames;				/* names are names of files */
	const char *description;	/* completion type - as a string */
	COMPL_ENTRY *candidate;		/* menu of completion candidates */
} PANEL_COMPL;

/********************************************************************/

typedef struct {
	PANEL_DESC *pd;			/* no additional data */
} PANEL_MENU;

/********************************************************************/

typedef struct {
	uid_t uid;
	const char *login;
	const char *gecos;
} USER_ENTRY;
	
typedef struct {
	PANEL_DESC *pd;
	USER_ENTRY *users;
	int usr_alloc;			/* allocated entries in 'users' */
} PANEL_USER;

typedef struct {
	gid_t gid;
	const char *group;
} GROUP_ENTRY;
	
typedef struct {
	PANEL_DESC *pd;
	GROUP_ENTRY *groups;
	int grp_alloc;		/* allocated entries in 'groups' */
} PANEL_GROUP;

/********************************************************************/

/* global variables */

extern const void *pcfg[CFG_VARIABLES];

extern DISPLAY display;
extern CLEX_DATA clex_data;
extern TEXTLINE *textline;		/* -> active line */
extern TEXTLINE line_cmd, line_dir, line_tmp;
extern PANEL_DESC *panel;		/* -> description of the active panel */
extern PANEL_FILE *ppanel_file;
extern PANEL_CFG panel_cfg;
extern PANEL_BM panel_bm_lst, panel_bm_mng;
extern PANEL_COMPL panel_compl;
extern PANEL_DIR panel_dir;	
extern PANEL_DIR_SPLIT panel_dir_split;	
extern PANEL_GROUP panel_group;
extern PANEL_HELP panel_help;
extern PANEL_HIST panel_hist;
extern PANEL_MENU panel_mainmenu, panel_compare, panel_paste;
extern PANEL_SORT panel_sort;
extern PANEL_USER panel_user;
extern CODE next_mode;			/* see control.c comments */
