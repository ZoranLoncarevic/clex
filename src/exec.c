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

#include <sys/types.h>		/* pid_t */
#include <errno.h>			/* errno */
#include <signal.h>			/* sigaction() */
#include <stdio.h>			/* puts() */
#include <stdlib.h>			/* exit() */
#include <string.h>			/* strcmp() */
#include <unistd.h>			/* fork() */

/* waitpid() */
#ifdef HAVE_SYS_WAIT_H
# include <sys/wait.h>
#endif
#ifndef WEXITSTATUS
# define WEXITSTATUS(status) ((unsigned)(status) >> 8)
#endif
#ifndef WIFEXITED
# define WIFEXITED(status) (((status) & 255) == 0)
#endif

#include "clex.h"
#include "exec.h"

#include "cfg.h"			/* config_str() */
#include "control.h"		/* get_current_mode() */
#include "edit.h"			/* edit_islong() */
#include "inout.h"			/* win_remark() */
#include "filepanel.h"		/* changedir() */
#include "history.h"		/* hist_save() */
#include "list.h"			/* list_directory() */
#include "tty.h"			/* tty_setraw() */
#include "undo.h"			/* undo_init() */
#include "util.h"			/* base_name() */
#include "userdata.h"		/* dir_tilde() */
#include "ustring.h"		/* us_copy() */
#include "xterm_title.h"	/* xterm_title_set() */

extern int errno;

#define MAX_SHELL_ARGS	8
static char *shell_argv[MAX_SHELL_ARGS + 2 + 1];
static int cmd_index;		/* which parameter is the command
							   to be executed */
#define MAX_NPL_CMDS 128
static char *nplist[MAX_NPL_CMDS+1];

void
exec_initialize(void)
{
	US_INIT(line_cmd.line);
	undo_init(&line_cmd);

	exec_shell_reconfig();
	exec_prompt_reconfig();	/* prompt needs to know the shell */
	exec_nplist_reconfig();
}

static int
split_str(char *str, char *substr[], char delimiter, int max)
{
	int i;
	FLAG arg_end;

	/* split to arguments */
	for (arg_end = 1, i = 0; *str; str++ )
		if (*str == delimiter) {
			*str = '\0';
			arg_end = 1;
		}
		else if (TCLR(arg_end)) {
			if (i >= max) return -1;  /* Error: too many substr*/
			substr[i++] = str;
		}
	substr[i] = 0;
	return i;
}

static int
shelltype(const char *shell)
{
	size_t len;

	shell = base_name(shell);
	len = strlen(shell);
	if (len >= 2 && strcmp(shell + len - 2,"sh") == 0)
		return (len >= 3 && shell[len - 3] == 'c');
	return 2;
}

static int
parse_shellprog(const char *shellprog)
{
	int i, ac;
	static USTRING dup = { 0,0 };

	us_copy(&dup,shellprog);
	ac = split_str(USTR(dup),shell_argv,' ',MAX_SHELL_ARGS);

	if (ac == -1) {
		txt_printf(
		  "CONFIG: Incorrect SHELLPROG: too many arguments\n"
		  "        limit is " STR(MAX_SHELL_ARGS) "\n");
		return -1;
	}

	for (cmd_index = 0, i = 1; i < ac; i++)
		if (strcmp(shell_argv[i],"<COMMAND>") == 0) {
			cmd_index = i;	/* real command belongs here */
			break;
		}

	/* append: -c <COMMAND> */
	if (cmd_index == 0) {
		shell_argv[ac] = "-c";
		cmd_index = ac + 1;
		shell_argv[ac + 2] = 0;
	}

	clex_data.shelltype = shelltype(shell_argv[0]);

	return 0;
}

/* prepare to execute commands according to "SHELLPROG" */
void
exec_shell_reconfig(void)
{
	if (*config_str(CFG_SHELLPROG)) {
		if (parse_shellprog(config_str(CFG_SHELLPROG)) == 0)
			return;		/* success */
		txt_printf("EXEC: Will use your login shell: %s\n",
		  clex_data.shell);
	}

	/* SHELLPROG = AUTO: loginshell -c <COMMAND> */
	parse_shellprog(clex_data.shell);
}

void
exec_prompt_reconfig(void)
{
	const char *prompt, *chars;
	static USTRING cmd_prompt = { 0,0 };
	static char prompt_char[4] = " ? ";

	prompt = config_str(CFG_PROMPT);

	if (prompt[0] == '\0') {
		/* PROMPT=AUTO */
		prompt = base_name(shell_argv[0]);
		switch (clex_data.shelltype) {
		case 0:
			chars = "$#";	/* Bourne shell or similar */
			break;
		case 1:
			chars = "%#";	/* C-shell or similar */
			break;
		default:
			chars = ">>";	/* some other shell */
			break;
		}
		prompt_char[1] = chars[clex_data.isroot];
	}
	else
		prompt_char[1] = '\0';

	us_cat(&cmd_prompt,clex_data.isroot ? "ROOT " : "",
	  prompt,prompt_char,(char *)0);
	edit_setprompt(&line_cmd,USTR(cmd_prompt));
}

void
exec_nplist_reconfig(void)
{
	static USTRING dup = { 0,0 };

	us_copy(&dup,config_str(CFG_NOPROMPT_CMDS));
	split_str(USTR(dup),nplist,' ',MAX_NPL_CMDS);
}

static int
execute(const char *command, FLAG prompt_user)
{
	pid_t childpid;
	FLAG failed;
	int status, code;
	struct sigaction act;
	const char *signame;

	failed = 1;
	xterm_title_set(1,command);
	childpid = fork();
	if (childpid == -1)
		printf("EXEC: Cannot create new process (%s)\n",
		  strerror(errno));
	else if (childpid == 0) {
		/* child process = command */
#ifdef _POSIX_JOB_CONTROL
		/* move this process to a new foreground process group */
		childpid = getpid();
		setpgid(childpid,childpid);
		tcsetpgrp(STDIN_FILENO,childpid);
#endif

		/* reset signal dispositions */
		act.sa_handler = SIG_DFL;
		act.sa_flags = 0;
		sigemptyset(&act.sa_mask);
		sigaction(SIGINT,&act,0);
		sigaction(SIGQUIT,&act,0);
#ifdef _POSIX_JOB_CONTROL
		sigaction(SIGTSTP,&act,0);
		sigaction(SIGTTIN,&act,0);
		sigaction(SIGTTOU,&act,0);
#endif

		/* execute the command */
		((const char **)shell_argv)[cmd_index] = command;
		execv(shell_argv[0],shell_argv);
		printf("EXEC: Cannot execute shell %s (%s)\n",
		  shell_argv[0],strerror(errno));
		exit(99);
		/* NOTREACHED */
	}
	else {
		/* parent process = CLEX */
#ifdef _POSIX_JOB_CONTROL
		/* move child process to a new foreground process group */
		setpgid(childpid,childpid);
		tcsetpgrp(STDIN_FILENO,childpid);
#endif
		for (; /* until break */;) {
			/* wait for child process exit or stop */
			while (waitpid(childpid,&status,WUNTRACED) < 0)
				/* ignore EINTR */;
#ifdef _POSIX_JOB_CONTROL
			/* move CLEX to foreground */
			tcsetpgrp(STDIN_FILENO,clex_data.pid);
#endif
			if (!WIFSTOPPED(status))
				break;

			puts(
				"\r\n"
				"\r\n"
				"EXEC: CLEX does not provide job control\r\n"
				"EXEC: suspended command will be resumed shortly\r\n"
				"\r\n");
			fflush(stdout);
#ifdef _POSIX_JOB_CONTROL
			/* move command back to foreground */
			tcsetpgrp(STDIN_FILENO,childpid);
#endif
			sleep (2);
			kill(-childpid,SIGCONT);
		}

		tty_reset();
		if (WIFEXITED(status)) {
			code = WEXITSTATUS(status);
			if (code == 0) {
				failed = 0;
				fputs("\nCommand successful. ",stdout);
			}
			else
				printf("\nExit code = %d. ",code);
		}
		else {
			code = WTERMSIG(status);
			printf("\nAbnormal termination, signal %d",code);
#ifdef HAVE_STRSIGNAL
			signame = strsignal(code);
			if (signame != 0)
				printf(" (%s)",signame);
#else
#if HAVE_DECL_SYS_SIGLIST
			signame = sys_siglist[code];
			if (signame != 0)
				printf(" (%s)",signame);
#endif
#endif

#ifdef WCOREDUMP
			if (WCOREDUMP(status))
				fputs(", core image dumped",stdout);
#endif
			putchar('\n');
		}
	}

	/*
	 * List the directory while the user stares at the screen
	 * reading the command output. This way CLEX appears to
	 * restart much faster. All output routines called from
	 * list_directory() must check if curses mode is active.
	 */
	xterm_title_set(0,command);
	list_directory();
	ppanel_file->other->expired = 1;

	if (prompt_user || failed) tty_press_enter();
	xterm_title_set(0,0);

	return failed;
}

static int
user_confirm(void)
{
	int in, confirmed;

	fputs("\nExecute the command ? (y = YES) ",stdout);
	fflush(stdout);
	tty_setraw();
	in = getchar();
	tty_reset();
	confirmed = in == 'y' || in == 'Y';
	puts(confirmed ? "yes\n" : "no\n");
	fflush(stdout);
	return confirmed;
}

static int
test_word(const char *line, const char *word)
{
	char ch;

	while (*line == ' ')
		line++;
	while ( (ch = *word++) != '\0')
		if (*line++ != ch)
			return 0;
	return 1;
}

/*
 * return value is the warning level:
 *   0 = no warning issued
 *   1 = only mandatory warning(s)
 *   2 = configurable warning(s)
 */
static int
print_warnings(const char *cmd)
{
	static USTRING cwd = { 0,0 };
	int warn;

	warn = 0;		/* 0 = no warning yet */

	if (get_cwd_us(&cwd) < 0) {
		puts("WARNING: current working directory is not accessible");
		warn = 1;	/* 1 = this warning cannot be turned off */
		us_copy(&cwd,"???");
	}
	else if (strcmp(USTR(ppanel_file->dir),USTR(cwd))) {
		printf("WARNING: current working directory has been renamed:\n"
			   "         old name: %s\n"
			   "         new name: %s\n",
		  USTR(ppanel_file->dir),USTR(cwd));
		us_copy(&ppanel_file->dir,USTR(cwd));
		warn = 1;
	}

	if (config_num(CFG_WARN_RM) && test_word(cmd,"rm ")) {
		printf("working directory: %s\n"
			   "WARNING: rm command deletes files, please confirm\n",
			USTR(cwd));
		warn = 2;	/* 2 = this warning can be turned off */
	}

	/* following warnings are not appropriate in history mode */
	if (get_current_mode() == MODE_FILE) {
		/*
		 * edit_islong() works with 'textline' instead of 'cmd',
		 * because it is meaningfull only in MODE_FILE
		 */
		if (config_num(CFG_WARN_LONG) && edit_islong()) {
			puts("WARNING: This long command did not fit to the"
			  " command line");
			warn = 2;
		}
	}

	fflush(stdout);
	return warn;
}

static const char *
check_cd(const char *str)
{
	static USTRING dir = { 0,0 };
	char ch;
	const char *dirstr;
	int i, real_len;
	FLAG quote, needs_dq, tilde;

	while (*str == ' ')
		str++;
	if (*str++ != 'c')
		return 0;
	if (*str++ != 'd')
		return 0;
	if (*str == '\0')
		return clex_data.homedir;	/* cd without args */
	if (*str++ != ' ')
		return 0;
	while (*str == ' ')
		str++;
	if (*str == '\0')
		return clex_data.homedir;	/* cd without args */

	tilde = *str == '~';
	quote = needs_dq = 0;
	for (real_len = 0, i = tilde /* 0 or 1 */; (ch = str[i]); i++) {
		if (quote)
			quote = 0;
		else if (ch == '\\')
			quote = needs_dq = 1;
		else if (ch == ' ') {
			if (real_len == 0)
				real_len = i;
		}
		else if (real_len || edit_isspecial((unsigned char)ch))
			return 0;	/*  not a simple 'cd' command */
	}

	if (needs_dq) {
		/* dequote: \x -> x */
		if (real_len == 0)
			real_len = i;
		us_setsize(&dir,real_len + 1);
		dequote_txt(str,real_len,USTR(dir));
		dirstr = USTR(dir);
	}
	else if (real_len) {
		/* trim off the trailing space */
		us_copyn(&dir,str,real_len);
		dirstr = USTR(dir);
	}
	else
		dirstr = str;

	return tilde ? dir_tilde(dirstr) : dirstr;
}

/*
 * function returns 1 if the command has been executed
 * (successfully or not), otherwise 0
 */
int
execute_cmd(const char *cmd, FLAG prompt_user)
{
	static FLAG hint = 1;
	int do_exec, warn_level, i;
	const char *dir, *s1, *s2;

	/* intercept single 'cd' command */
	if ( (dir = check_cd(cmd)) ) {
		if (changedir(dir) != 0)
			return 0;
		hist_save(cmd,0);
		win_heading();
		win_panel();
		win_remark("directory changed");
		return 1;
	}

	/* is cmd in the list of interactive commands?  */
	if (prompt_user) {
		for(i=0; nplist[i]; i++)
		{
			s1 = nplist[i]; s2 = cmd;
			while(*s1 && *s2 && *s1++==*s2++) ;
			if (*s1=='\0' && (*s2==' ' || *s2=='\0'))
			{ prompt_user = 0; curses_stop_with_smcup(); break; }
		}
	}

	if (prompt_user) {
		curses_stop();
		putchar('\n');
		puts(cmd);
		putchar('\n');
		fflush(stdout);
	}

	warn_level = print_warnings(cmd);
	do_exec = warn_level == 0 || user_confirm();
	if (do_exec)
		hist_save(cmd,execute(cmd,prompt_user) != 0);

	curses_restart();
	if (warn_level >= 2 && TCLR(hint))
		win_remark("warnings can be turned off in the config panel");

	return do_exec;
}
