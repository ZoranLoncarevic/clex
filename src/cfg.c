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

#include <sys/types.h>	/* mode_t */
#include <sys/stat.h>	/* stat() */
#include <errno.h>		/* errno */
#include <fcntl.h>		/* open() */
#include <stdio.h>		/* sprintf() */
#include <string.h>		/* strchr() */
#include <unistd.h>		/* stat() */

#include "clex.h"
#include "cfg.h"

#include "completion.h"	/* completion_reconfig() */
#include "control.h"	/* control_loop() */
#include "directory.h"	/* dir_reconfig() */
#include "edit.h"		/* edit_putstr() */
#include "exec.h"		/* exec_shell_reconfig() */
#include "help.h"		/* help_reconfig() */
#include "history.h"	/* hist_reconfig() */
#include "inout.h"		/* win_remark() */
#include "list.h"		/* list_reconfig() */
#include "sort.h"		/* sort_files() */
#include "ustring.h"	/* USTR() */
#include "util.h"		/* estrdup() */
#include "xterm_title.h"/* xterm_reconfig() */

#define CFG_LINES_LIMIT 100		/* config file size limit (in lines) */
#define CFG_ERRORS_LIMIT  12	/* error limit */

extern int errno;

static const char *user_config_file; /* personal cfg filename */

/* used in error handling: */
static const char *cfgfile;		/* name of the current cfg file */
static int cfgerror;			/* error counter */

typedef struct {
	CODE code;					/* CFG_XXX */
	const char *extra_val;		/* if defined - name of special value
								   (represented as 0) */
	int min, max, initial, current, new;	/* values */
	const char *desc[4];		/* if defined - show this text instead
								   of numbers (enumerated type) */
} CNUM;

static CNUM table_numeric[] = {
	/* enumerated */
	{ CFG_CMD_LINES,	0, 2, 4, 2, 0, 0,
		{	"2 screen lines",
			"3 screen lines",
			"4 screen lines" } },
	{ CFG_KILOBYTE,	0, 0, 1, 0, 0, 0,
		{	"1 KiB is 1024 bytes (IEC standard)",
			"1 KB is 1000 bytes (SI standard)" } },
	{ CFG_FMT_NUMBER,	0, 0, 2, 0, 0, 0,
		{	"AUTO (according to the current locale)",
			"1.000.000 (dot separated)",
			"1,000,000 (comma separated)" } },
	{ CFG_FMT_TIME,		0, 0, 2, 0, 0, 0,
		{	"AUTO (according to the current locale)",
			"12 hour clock",
			"24 hour clock" } },
	{ CFG_COLLATION,	0, 0, 1, 1, 0, 0,
		{	"byte order (based on the character codes)",
			"dictionary order (based on the human language)" } },
	{ CFG_FRAME,		0, 0, 2, 0, 0, 0,
		{	"-----",
			"=====",
			"line graphics (not supported on some terminals)" } },
	{ CFG_GROUP_FILES,	0, 0, 2, 2, 0, 0,
		{	"Do not group",
			"Group: directories, special files, plain files",
			"Group: directories, devices, special files, plain files" }
	},
	{ CFG_LAYOUT,		0, 0, 2, 0, 0, 0,
		{	"Layout #1",
			"Layout #2",
			"Layout #3" } },
	{ CFG_WARN_RM,		0, 0, 1, 1, 0, 0, { "No", "Yes" } },
	{ CFG_WARN_LONG,	0, 0, 1, 1, 0, 0, { "No", "Yes" } },
	{ CFG_WARN_SELECT,	0, 0, 1, 1, 0, 0, { "No", "Yes" } },
	{ CFG_XTERM_TITLE,	0, 0, 2, 1, 0, 0,
		{	"No",
			"AUTO (checking the terminal type $TERM)",
			"Yes" } },
	/* really numeric */
	{ CFG_C_SIZE,		"AUTO",	10, 100,  0, 0, 0, { 0 } },
	{ CFG_D_SIZE,		"AUTO",	10, 100,  0, 0, 0, { 0 } },
	{ CFG_H_SIZE,		0,		10, 100, 40, 0, 0, { 0 } },
	{ CFG_SHOW_HIDDEN,	0, 0, 2, 0, 0, 0,
		{	"Show hidden .files",
			"Show hidden .files, except in home directory",
			"Do not show hidden .files" } },
	{ CFG_SHOW_LINKTRGT,	0, 0, 1, 0, 0, 0,
		{	"Show link target",
			"Do not show link target  " } },
};

typedef struct {
	CODE code;					/* CFG_XXX */
	const char *extra_val;		/* if defined - name of special value
								   (represented as "") */
	char *initial;
	char current[CFGVALUE_LEN + 1], new[CFGVALUE_LEN + 1];
} CSTR;

static CSTR table_string[] = {
	{ CFG_CMD_F3,		0,		"more $f",		"", "" },
	{ CFG_CMD_F4,		0,		"vi $f",		"", "" },
	{ CFG_CMD_F5,		0,		"cp -ir $f $2",	"", "" },
	{ CFG_CMD_F6,		0,		"mv -i $f $2",	"", "" },
	{ CFG_CMD_F7,		0,		"mkdir ",		"", "" },
	{ CFG_CMD_F8,		0,		"rm $f",		"", "" },
	{ CFG_CMD_F9,		0,		"lpr $f",		"", "" },
	{ CFG_CMD_F10,		0,		"",				"", "" },
	{ CFG_CMD_F11,		0,		"",				"", "" },
	{ CFG_CMD_F12,		0,		"",				"", "" },
	{ CFG_DIR2,			"HOME",	"",				"", "" },
	{ CFG_FMT_DATE,		"AUTO",	"",				"",	"" },
	{ CFG_HELPFILE,		"NONE",	"",				"", "" },
	{ CFG_LAYOUT1,		0, "$d $S $>$t $M $*|  $p $o $L", "", "" },
	{ CFG_LAYOUT2,		0, "$d $S $t $*|  $p $o", "", "" },
	{ CFG_LAYOUT3,		0,
	  "$p $o $s $d $>$t $*|  mode=$m atime=$a ctime=$i links=$l", "", "" },
	{ CFG_PROMPT,		"AUTO",	"",				"", "" },
	{ CFG_QUOTE,		0,		"",				"", "" },
	{ CFG_SHELLPROG,	"AUTO",	"",				"", "" },
	{ CFG_VIEWER_CMD,		0,		"less $f",		"", "" },
	{ CFG_NOPROMPT_CMDS,		0,		"less man vi vim",		"", "" }
};

/*
 * help - everything in one place and alphabetically sorted for easy editing,
 * text should fit on minimal width screen
 */
static struct {
	CODE code;
	char *help;
} table_help[CFG_VARIABLES] = {
	{ CFG_C_SIZE,		"Completion panel size (AUTO = screen size)" },
	{ CFG_CMD_F3,		"Command F3 = view file(s)" },
	{ CFG_CMD_F4,		"Command F4 = edit file(s)" },
	{ CFG_CMD_F5,		"Command F5 = copy file(s)" },
	{ CFG_CMD_F6,		"Command F6 = move file(s)" },
	{ CFG_CMD_F7,		"Command F7 = make directory" },
	{ CFG_CMD_F8,		"Command F8 = remove file(s)" },
	{ CFG_CMD_F9,		"Command F9 = print file(s)" },
	{ CFG_CMD_F10,		"Command F10 = user defined" },
	{ CFG_CMD_F11,		"Command F11 = user defined" },
	{ CFG_CMD_F12,		"Command F12 = user defined" },
	{ CFG_CMD_LINES,	"Appearance: "
		"How many lines are occupied by the input line" },
	{ CFG_COLLATION,	"Appearance: Filename collation sequence" },
	{ CFG_D_SIZE,		"Directory panel size (AUTO = screen size)" },
	{ CFG_DIR2,			"Secondary file panel's initial directory "
						"(HOME = home dir)" },
	{ CFG_FRAME,		"Appearance: "
		"Panel frame: ----- or ===== or line graphics" },
	{ CFG_FMT_DATE,		"Appearance: "
		"Format: date (e.g. dMy, y/m/d), or AUTO" },
	{ CFG_FMT_NUMBER,	"Appearance: Format: thousands separator" },
	{ CFG_FMT_TIME,		"Appearance: Format: 12/24 hour clock" },
	{ CFG_GROUP_FILES,	"Appearance: "
		"Group files of the same type together" },
	{ CFG_H_SIZE,		"History panel size" },
	{ CFG_HELPFILE,		"External helpfile "
						"(NONE = use built-in help instead)" },
	{ CFG_SHOW_HIDDEN,	"Appearance: Wether to show hidden .files" },
	{ CFG_SHOW_LINKTRGT,	"Appearance: Wether to show link targets" },
	{ CFG_KILOBYTE,		"Appearance: Filesize unit definition" },
	{ CFG_LAYOUT,		"Appearance: "
		"Which file panel layout is active" },
	{ CFG_LAYOUT1,		"Appearance: File panel layout #1, see help" },
	{ CFG_LAYOUT2,		"Appearance: File panel layout #2" },
	{ CFG_LAYOUT3,		"Appearance: File panel layout #3" },
	{ CFG_NOPROMPT_CMDS,	"List of interactive commands, see help" },
	{ CFG_PROMPT,		"Appearance: "
		"Command line prompt (AUTO = according to shell)" },
	{ CFG_QUOTE,		"Additional filename chars to be quoted, "
						"see help" },
	{ CFG_SHELLPROG,	"Shell program, see help "
						"(AUTO = your login shell)" },
	{ CFG_VIEWER_CMD,	"File viewer command" },
	{ CFG_WARN_RM,		"Warn before executing 'rm' (remove) command" },
	{ CFG_WARN_LONG,	"Warn that the command line is too long to be "
						"displayed" },
	{ CFG_WARN_SELECT,	"Remind that you have selected some files" },
	{ CFG_XTERM_TITLE,	"Appearance: "
		"Change the X terminal window title" }
};

static CONFIG_ENTRY config[CFG_VARIABLES] = {
	/* do not exceed max length of CFGVAR_LEN */
	{ "FRAME",			0,0,0,0,0 },
	{ "CMD_LINES",		0,0,0,0,0 },
	{ "XTERM_TITLE",	0,0,0,0,0 },
	{ "GROUP_FILES",	0,0,0,0,0 },
	{ "PROMPT",			0,0,0,0,0 },
	{ "LAYOUT1",		0,0,0,0,0 },
	{ "LAYOUT2",		0,0,0,0,0 },
	{ "LAYOUT3",		0,0,0,0,0 },
	{ "ACTIVE_LAYOUT",	0,0,0,0,0 },
	{ "KILOBYTE",		0,0,0,0,0 },
	{ "FMT_NUMBER",		0,0,0,0,0 },
	{ "FMT_TIME",		0,0,0,0,0 },
	{ "FMT_DATE",		0,0,0,0,0 },
	{ "COLLATION",		0,0,0,0,0 },
	{ "SHELLPROG",		0,0,0,0,0 },
	{ "CMD_F3",			0,0,0,0,0 },
	{ "CMD_F4",			0,0,0,0,0 },
	{ "CMD_F5",			0,0,0,0,0 },
	{ "CMD_F6",			0,0,0,0,0 },
	{ "CMD_F7",			0,0,0,0,0 },
	{ "CMD_F8",			0,0,0,0,0 },
	{ "CMD_F9",			0,0,0,0,0 },
	{ "CMD_F10",		0,0,0,0,0 },
	{ "CMD_F11",		0,0,0,0,0 },
	{ "CMD_F12",		0,0,0,0,0 },
	{ "WARN_RM",		0,0,0,0,0 },
	{ "WARN_LONG",		0,0,0,0,0 },
	{ "WARN_SELECT",	0,0,0,0,0 },
	{ "DIR2",			0,0,0,0,0 },
	{ "HELPFILE",		0,0,0,0,0 },
	{ "QUOTE",			0,0,0,0,0 },
	{ "C_PANEL_SIZE",	0,0,0,0,0 },
	{ "D_PANEL_SIZE",	0,0,0,0,0 },
	{ "H_PANEL_SIZE",	0,0,0,0,0 },
	{ "VIEWER_CMD",		0,0,0,0,0 },
	{ "NOPROMPT_CMDS",	0,0,0,0,0 },
	{ "SHOW_HIDDEN",	0,0,0,0,0 },
	{ "SHOW_LINKTRGT",	0,0,0,0,0 }
};	/* must exactly match CFG_XXX #defines */

/* 'move' values MOV_X2Y understood by set_value() */
#define MOV_SRC_CURRENT	 1
#define MOV_SRC_INITIAL	 2
#define MOV_SRC_NEW		 4
#define MOV_DST_CURRENT	 8
#define MOV_DST_NEW		16
#define MOV_N2C	(MOV_SRC_NEW     | MOV_DST_CURRENT)
#define MOV_C2N	(MOV_SRC_CURRENT | MOV_DST_NEW)
#define MOV_I2N	(MOV_SRC_INITIAL | MOV_DST_NEW)
#define MOV_I2C	(MOV_SRC_INITIAL | MOV_DST_CURRENT)

static void
set_value(int code, int move)
{
	int src_num, *dst_num;
	const char *src_str;
	char *dst_str;
	CNUM *pnum;
	CSTR *pstr;

	if (config[code].isnum) {
		pnum = config[code].table;
		if (move & MOV_SRC_CURRENT)
			src_num = pnum->current;
		else if (move & MOV_SRC_INITIAL)
			src_num = pnum->initial;
		else
			src_num = pnum->new;
		if (move & MOV_DST_CURRENT)
			dst_num = &pnum->current;
		else
			dst_num = &pnum->new;
		*dst_num = src_num;
	}
	else {
		pstr = config[code].table;
		if (move & MOV_SRC_CURRENT)
			src_str = pstr->current;
		else if (move & MOV_SRC_INITIAL)
			src_str = pstr->initial;
		else
			src_str = pstr->new;
		if (move & MOV_DST_CURRENT)
			dst_str = pstr->current;
		else
			dst_str = pstr->new;
		strcpy(dst_str,src_str);
	}
}

static CONFIG_ENTRY *
get_variable(const char *var)
{
	int i;

	for (i = 0; i < CFG_VARIABLES; i++)
		if (strcmp(config[i].var,var) == 0)
			return config + i;
	return 0;
}

static CNUM *
get_numeric(int code)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(table_numeric); i++)
		if (table_numeric[i].code == code)
			return table_numeric + i;
	return 0;
}

static CSTR *
get_string(int code)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(table_string); i++)
		if (table_string[i].code == code)
			return table_string + i;
	return 0;
}

static const char *
get_help(int code)
{
	int i;
	const char *helpstr;

	for (i = 0; i < CFG_VARIABLES; i++)
		if (table_help[i].code == code) {
			helpstr = table_help[i].help;
			if (strlen(helpstr) > MIN_COLS - 4)
				err_exit("BUG: help string \"%s\" too long\n",helpstr);
			return helpstr;
		}
	return 0;
}

static void
config_error_init(const char *filename)
{
	cfgfile = filename;
	cfgerror = 0;
}

static void
config_error(int line, const char *errmsg)
{
	if (cfgerror == 0)
		txt_printf("CONFIG: Parsing configuration file \"%s\"\n",
		  cfgfile);
	fputs("CONFIG: ",stdout);
	if (line > 0)
		printf("Line %d: ",line);
	puts(errmsg);

	if (++cfgerror > CFG_ERRORS_LIMIT)
		err_exit("CONFIG: Too many errors");
}

static void
read_configuration(const char *file, int setdefaults)
{
	FILE *fp;
	char buff[CFGVAR_LEN + CFGVALUE_LEN + 16], *value;
	int len, ilen, nvalue, line;
	struct stat fst;
	CONFIG_ENTRY *pce;
	CNUM *pnum;
	CSTR *pstr;

	config_error_init(file);

	/* few basic checks */
	if (stat(file,&fst) < 0 ) {
		/* it is OK if the file is missing */
		if (errno != ENOENT)
			config_error(0,"File is unreadable");
		return;
	}
	if ((fst.st_mode & S_IWOTH) == S_IWOTH) {
		/* little bit of security */
		config_error(0,"File is unsafe (world-writable), skipped");
		return;
	}
	if (!S_ISREG(fst.st_mode)) {
		/* sanity check */
		config_error(0,"This is not a plain file");
		return;
	}

	/*
	 * yes, there is a race condition here, but the checks
	 * above are merely detecting stupid mistakes
	 */

	fp = fopen(file,"r");
	if (fp == 0) {
		config_error(0,"File is unreadable");
		return;	
	}
	for (line = 1; fgets(buff,sizeof(buff),fp); line++) {
		if (line > CFG_LINES_LIMIT) {
			config_error(line,
			  "file is too long, skipping the rest of this file");
			break;
		}

		/* ignore comments */
		if (buff[0] == '\n' || buff[0] == '#'
		  || buff[0] == '\0' /* can fgets() return empty string ? */)
			continue;

		/* strip newline */
		len = strlen(buff) - 1;
		if (buff[len] != '\n') {
			config_error(line,
			  "line is too long, skipping the rest of this file");
			break;	/* why bother to recover ? */
		}
		buff[len] = '\0';

		/* split VARIABLE and VALUE */
		if ( (value = strchr(buff,'=')) == 0) {
			config_error(line,
			  "incorrect syntax, format: VARIABLE=VALUE");
			continue;
		}
		*value++ = '\0';
		len -= value - buff;

		pce = get_variable(buff);
		if (pce == 0) {
			config_error(line,"unknown variable");
			continue;
		}

		if (pce->isnum) {
			pnum = pce->table;
			if (sscanf(value," %d %n",&nvalue,&ilen) < 1 || ilen != len)
				config_error(line,"invalid number");
			else if ((nvalue < pnum->min || nvalue > pnum->max)
			  && (nvalue != 0 || pnum->extra_val == 0))
				config_error(line,"numeric value out of range");
			else if (setdefaults)
				pnum->initial = nvalue;
			else
				pnum->current = nvalue;
		} else {
			pstr = pce->table;
			if (len > CFGVALUE_LEN)
				config_error(line,"string value is too long");
			else if (setdefaults)
				pstr->initial = estrdup(value);
			else
				strcpy(pstr->current,value);
		}
	}
	if (ferror(fp))
		config_error(0,"File read error");
	fclose(fp);
}

void
config_initialize(void)
{
	int i, fd;
	CNUM *pnum;
	CSTR *pstr;

	/* initialize 'config' & 'pcfg' tables */
	for (i = 0; i < CFG_VARIABLES; i++) {
		if ( (pnum = get_numeric(i)) ) {
			config[i].isnum = 1;
			config[i].table = pnum;
			pcfg[i] = &pnum->current;
		}
		else if ( (pstr = get_string(i)) ) {
			config[i].table = pstr;
			pcfg[i] = &pstr->current;
		}
		else
			err_exit("BUG: config variable not defined (code %d)",i);
		if ( (config[i].help = get_help(i)) == 0)
			err_exit("BUG: no help for config variable (code %d)",i);
	}

	if (clex_data.admin) {
		/*
		 * check user's access rights
		 * this may create an empty file, never mind
		 */
		umask(022);
		fd = open(CONFIG_FILE,O_WRONLY | O_CREAT,0644);
		umask(clex_data.umask);
		if (fd < 0)
			err_exit(
			  "You are not allowed to create/modify the system-wide\n"
			  "    configuration file \"%s\" (%s)",
			  CONFIG_FILE,strerror(errno));
		close(fd);

		/* initialize, read defaults */
		for (i = 0; i < CFG_VARIABLES; i++)
			set_value(i,MOV_I2C);
		read_configuration(CONFIG_FILE,0);
	}
	else {
		/* user's configuration file name */
		pathname_set_directory(clex_data.homedir);
		user_config_file = estrdup(pathname_join(".clexrc"));

		/* read defaults, initialize, read config */
		read_configuration(CONFIG_FILE,1);
		for (i = 0; i < CFG_VARIABLES; i++)
			set_value(i,MOV_I2C);
		read_configuration(user_config_file,0);
	}

	panel_cfg.config = config;
}

/* the 'new' value in readable text form */
const char *
config_print_value(int i)
{
	static char buff[16];
	CNUM *pnum;
	CSTR *pstr;

	/* string */
	if (!config[i].isnum) {
		pstr = config[i].table;
		if (pstr->extra_val != 0 && *pstr->new == '\0')
			return pstr->extra_val;
		return pstr->new;
	}

	/* numeric */
	pnum = config[i].table;
	if (pnum->extra_val != 0 && pnum->new == 0)
		return pnum->extra_val;
	if (pnum->desc[0])
		/* numeric - enumerated */
		return pnum->desc[pnum->new - pnum->min];
	/* really numeric */
	sprintf(buff,"%d",pnum->new);
	return buff;
}

static void
config_save(void)
{
	int i;
	FLAG errflag;
	FILE *fp;

	if (clex_data.admin) {
		umask(022);
		fp = fopen(CONFIG_FILE,"w");
	} else {
		umask(clex_data.umask | 022);
		fp = fopen(user_config_file,"w");
	}
	umask(clex_data.umask);

	if (fp == 0) {
		win_warning("CONFIG: Cannot open configuration file "
		  "for writing.");
		return;
	}
	fprintf(fp,	"#\n"
				"# CLEX configuration file\n"
				"#\n");
	for (i = 0; i < CFG_VARIABLES; i++)
		if (config[i].saveit) {
			if (config[i].isnum)
				fprintf(fp,"%s=%d\n",config[i].var,
				  ((CNUM *)config[i].table)->new);
			else
				fprintf(fp,"%s=%s\n",config[i].var,
				  ((CSTR *)config[i].table)->new);
		}
	errflag = ferror(fp) != 0;
	if (fclose(fp) || errflag)
		win_warning("CONFIG: File write error occurred.");
	else
		win_remark("configuration saved");
}

void
config_prepare(void)
{
	int i;

	for (i = 0; i < CFG_VARIABLES; i++)
		set_value(i,MOV_C2N);
	panel_cfg.pd->top = panel_cfg.pd->curs = panel_cfg.pd->min;
	panel = panel_cfg.pd;
	textline = 0;
}

void
config_edit_num_prepare(void)
{
	static char prompt[CFGVAR_LEN + 48];
	CNUM *pnum;

	/* inherited panel = panel_cfg.pd */
	pnum = config[panel_cfg.pd->curs].table;
	textline = &line_tmp;
	sprintf(prompt,"%s (range: %d - %d%s%s): ",
	  config[panel_cfg.pd->curs].var,pnum->min,pnum->max,
	  pnum->extra_val != 0 ? " or " : "",
	  pnum->extra_val != 0 ? pnum->extra_val : "");
	edit_setprompt(textline,prompt);

	edit_nu_putstr(config_print_value(panel_cfg.pd->curs));
}

void
config_edit_str_prepare(void)
{
	static char prompt[CFGVAR_LEN + 32];
	CSTR *pstr;

	/* inherited panel = panel_cfg.pd */
	pstr = config[panel_cfg.pd->curs].table;
	textline = &line_tmp;
	sprintf(prompt,"%s (%d chars max%s%s): ",
	  config[panel_cfg.pd->curs].var,CFGVALUE_LEN,
	  pstr->extra_val != 0 ? " or " : "",
	  pstr->extra_val != 0 ? pstr->extra_val : "");
	edit_setprompt(textline,prompt);

	edit_nu_putstr(config_print_value(panel_cfg.pd->curs));
}

void
cx_config_num_enter(void)
{
	int nvalue, ilen;
	CNUM *pnum;

	pnum = config[panel_cfg.pd->curs].table;
	if (pnum->extra_val != 0
	  && strcmp(USTR(textline->line),pnum->extra_val) == 0) {
		pnum->new = 0;
		next_mode = MODE_SPECIAL_RETURN;
		return;
	}

	if (sscanf(USTR(textline->line)," %d %n",&nvalue,&ilen) < 1
	  || ilen != textline->size)
		win_remark("numeric value required");
	else if (nvalue < pnum->min || nvalue > pnum->max)
		win_remark("value is out of range");
	else {
		pnum->new = nvalue;
		next_mode = MODE_SPECIAL_RETURN;
	}
}

void
cx_config_str_enter(void)
{
	CSTR *pstr;

	pstr = config[panel_cfg.pd->curs].table;
	if (pstr->extra_val != 0
	  && strcmp(USTR(textline->line),pstr->extra_val) == 0) {
		*pstr->new = '\0';
		next_mode = MODE_SPECIAL_RETURN;
		return;
	}

	if (textline->size > CFGVALUE_LEN)
		win_remark("string is too long");
	else {
		strcpy(pstr->new,USTR(textline->line));
		next_mode = MODE_SPECIAL_RETURN;
	}
}

void
cx_config_default(void)
{
	set_value(panel_cfg.pd->curs,MOV_I2N);
	win_panel_opt();
}

void
cx_config_original(void)
{
	set_value(panel_cfg.pd->curs,MOV_C2N);
	win_panel_opt();
}

/* detect what has changed */
static void
config_changes(void)
{
	int i;
	CNUM *pnum;
	CSTR *pstr;

	for (i = 0; i < CFG_VARIABLES; i++)
		if (config[i].isnum) {
			pnum = config[i].table;
			config[i].changed = pnum->new != pnum->current;
			config[i].saveit = pnum->new != pnum->initial;
		}
		else {
			pstr = config[i].table;
			config[i].changed = strcmp(pstr->new,pstr->current) != 0;
			config[i].saveit = strcmp(pstr->new,pstr->initial) != 0;
		}
}

static void
config_apply(void)
{
	int i;
	FLAG reread, sort, prompt;

	for (i = 0; i < CFG_VARIABLES; i++)
		if (config[i].changed)
			set_value(i,MOV_N2C);

	reread = sort = prompt = 0;
	if (config[CFG_FRAME].changed) {
		win_frame_reconfig();
		win_frame();
	}
	if (config[CFG_CMD_LINES].changed)
		/* all it takes is to say it ... isn't it magic ?! */
		txt_printf("SCREEN: changing geometry\n");
	if (config[CFG_XTERM_TITLE].changed) {
		xterm_title_restore();
		xterm_title_reconfig();
		xterm_title_set(0,0);
	}
	if (config[CFG_GROUP_FILES].changed
	  || config[CFG_COLLATION].changed)
		sort = 1;
	if (config[CFG_PROMPT].changed)
		prompt = 1;
	if (config[CFG_LAYOUT].changed
	  || config[CFG_LAYOUT1].changed
	  || config[CFG_LAYOUT2].changed
	  || config[CFG_LAYOUT3].changed) {
		win_layout_reconfig();
		reread = 1;
	}
	if (config[CFG_FMT_NUMBER].changed
	  || config[CFG_FMT_TIME].changed
	  || config[CFG_FMT_DATE].changed
	  || config[CFG_KILOBYTE].changed)
		reread = 1;
	if (config[CFG_SHOW_HIDDEN].changed)
		reread = 1;
	if (config[CFG_SHELLPROG].changed) {
		exec_shell_reconfig();
		prompt = 1;
	}
	if (config[CFG_HELPFILE].changed)
		help_reconfig();
	if (config[CFG_C_SIZE].changed)
		completion_reconfig();
	if (config[CFG_D_SIZE].changed)
		dir_reconfig();
	if (config[CFG_H_SIZE].changed)
		hist_reconfig();
	if (config[CFG_NOPROMPT_CMDS].changed)
		exec_nplist_reconfig();

	if (prompt)
		exec_prompt_reconfig();
	if (sort && !reread) {
		/* sort the primary panel */
		filepos_save();
		sort_files();
		filepos_set();
		/* sort the secondary panel */
		/*
		 * warning: during the sorting of the secondary panel,
		 * the primary panel does not correspond with the current
		 * working directory
		 */
		ppanel_file = ppanel_file->other;
		filepos_save();
		sort_files();
		filepos_set();
		ppanel_file = ppanel_file->other;
	}
	else if (reread) {
		ppanel_file->other->expired = 1;
		list_reconfig();
		list_directory();
	}

	/* xxx_reconfigure() might have suspended curses mode */
	if (!display.curses)
		curses_restart();
}

void
cx_config_admin_save(void)
{
	config_changes();
	config_save();
}

void
cx_config_save(void)
{
	/* cursor -2 = ACCEPT, -1 = SAVE */
	config_changes();
	config_apply();
	if (panel_cfg.pd->curs == -1)
		config_save();
}

void
cx_config_enter(void)
{
	CNUM *pnum;

	if (config[panel_cfg.pd->curs].isnum) {
		pnum = config[panel_cfg.pd->curs].table;
		if (pnum->desc[0]) {
			if (++pnum->new > pnum->max)
				pnum->new = pnum->min;
		}
		else
			control_loop(MODE_CFG_EDIT_NUM);
	}
	else
		control_loop(MODE_CFG_EDIT_TXT);
	win_panel_opt();
}

void
config_set_num(int code, int new_value)
{
	CNUM *pnum = config[code].table;

	pnum->new = new_value;
	config[code].changed = pnum->new != pnum->current;
	config[code].saveit = pnum->new != pnum->initial;
	config_apply();
}

void
config_set_str(int code, char *new_value)
{
	CSTR *pstr = config[code].table;

	strcpy(pstr->new,new_value);
	config[code].changed = strcmp(pstr->new,pstr->current) != 0;
	config[code].saveit = strcmp(pstr->new,pstr->initial) != 0;
	config_apply();
}
