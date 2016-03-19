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
#include <ctype.h>		/* iscntrl() */
#include <signal.h>		/* signal() */
#include <stdarg.h>		/* va_list */
#include <stdio.h>		/* printf() */
#include <stdlib.h>		/* getenv() */
#include <string.h>		/* strchr() */
#include <unistd.h>		/* fork() */

/* waitpid() */
#ifdef HAVE_SYS_WAIT_H
# include <sys/wait.h>
#endif

#include "clex.h"
#include "xterm_title.h"

#include "cfg.h"		/* config_num() */
#include "util.h"		/* estrdup() */

static FLAG enabled = 0;
static const char *oldtitle = 0;

#define XPROP_TIMEOUT	6	/* timeout for xprop command in seconds */
#define CMD_STR			20	/* name of executed command in the title */

void
xterm_title_initialize(void)
{
	xterm_title_reconfig();
	xterm_title_set(0,0);
}

/*
 * run the command
 *   xprop -id $WINDOWID WM_NAME
 * to get the current xterm title
 *
 * errors are not reported, we don't want to confuse the user
 */
static const char *
get_title(void)
{
	int fd[2];
	ssize_t rd;
	const char *wid;
	char *p1, *p2, title[128];
	pid_t pid;
	struct sigaction act;

	if ( (wid = getenv("WINDOWID")) == 0)
		return 0;

	if (pipe(fd) < 0 || (pid = fork()) < 0)
		return 0;

	if (pid == 0) {
		/* this is the child process */
		close(fd[0]);	/* close read end */
		if (fd[1] != STDOUT_FILENO) {
			if (dup2(fd[1], STDOUT_FILENO) != STDOUT_FILENO)
				_exit(126);
			close(fd[1]);
		}

		act.sa_handler = SIG_DFL;
		act.sa_flags = 0;
		sigemptyset(&act.sa_mask);
		sigaction(SIGALRM,&act,0);
		alarm(XPROP_TIMEOUT);

		execlp("xprop", "xprop", "-id", wid, "WM_NAME", (char *)0);
		_exit(127);
	}

	/* parent continues here */
	/* do not return before waitpid() or you create a zombie */
	close(fd[1]);	/* close write end */
	rd = read_fd(fd[0],title,sizeof(title) - 1);
	close(fd[0]);

	if (waitpid(pid, 0, 0) < 0)
		return 0;

	if (rd == -1)
		return 0;

	title[rd] = '\0';
	/* get the window title in quotation marks */
	p1 = strchr(title,'\"');
	if (p1 == 0)
		return 0;
	p2 = strchr(++p1,'\"');
	if (p2 == 0)
		return 0;
	*p2 = '\0';

	return estrdup(p1);
}

void
xterm_title_reconfig(void)
{
	const char *term;

	enabled = 0;
	/* 0 = disabled */
	if (config_num(CFG_XTERM_TITLE) == 0)
		return;

	/* 1 = automatic -> check $TERM for xterm, kterm or dtterm */
	if (config_num(CFG_XTERM_TITLE) == 1) {
		if ( (term = getenv("TERM")) == 0)
			return;
		if (strncmp(term,"xterm",5) && strncmp(term,"kterm",5)
		  && strncmp(term,"dtterm",6))
			return;
	}
	/* otherwise 2 = enabled */
	if (oldtitle == 0 && (oldtitle = get_title()) == 0)
		oldtitle = "terminal";
	enabled = 1;
}

static void
set_xtitle(const char *str1, ...)
{
	va_list argptr;
	const char *strn;

	fputs("\033]0;",stdout);

	fputs(str1,stdout);
	va_start(argptr,str1);
	while ( (strn = va_arg(argptr, char *)) )
		fputs(strn,stdout);
	va_end(argptr);

	/* Xterm FAQ claims \007 is incorrect, but everybody uses it */
	fputs("\007",stdout);
	fflush(stdout);
}

void
xterm_title_set(int busy, const char *cmd)
{
	char short_cmd[CMD_STR];
	FLAG longcmd;
	int i, ch;

	if (!enabled)
		return;

	if (cmd == 0) {
		/* CLEX is idle */
		set_xtitle("clex: ", clex_data.login_at_host, (char *)0);
		return;
	}

	/* CLEX is executing or was executing 'cmd' */
	for (longcmd = 0, i = 0; (ch = (unsigned char)cmd[i]); i++) {
		if (i == CMD_STR - 1) {
			longcmd = 1;
			break;
		}
		short_cmd[i] = iscntrl(ch) ? '?' : ch;
	}
	short_cmd[i] = '\0';
	set_xtitle("clex: ", busy ? "" : "(", short_cmd,
	  longcmd ? "..." : "", busy ? "" : ")", (char *)0);
}

/* note: this is a cleanup function */
void
xterm_title_restore(void)
{
	if (enabled)
		set_xtitle(oldtitle,(char *)0);
}
