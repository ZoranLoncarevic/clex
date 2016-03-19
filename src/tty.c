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

#include <sys/types.h>	/* pid_t */
#include <errno.h>		/* errno */
#include <fcntl.h>		/* fcntl */
#include <stdio.h>		/* fputs() */
#include <termios.h>	/* struct termios */
#include <unistd.h>		/* STDIN_FILENO */

#include "clex.h"
#include "tty.h"

#include "control.h"	/* err_exit() */

extern int errno;

static struct termios text_termios, raw_termios, *ptermios = 0;
#ifdef _POSIX_JOB_CONTROL
static pid_t save_pgid = 0;
#endif

void
tty_initialize(void)
{
	if (tcgetattr(STDIN_FILENO,&text_termios) < 0)
		err_exit("Cannot read the terminal settings");
	ptermios = &text_termios;

	raw_termios = text_termios;	/* struct copy */
	raw_termios.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
	raw_termios.c_cc[VMIN] = 1;
	raw_termios.c_cc[VTIME] = 0;

#ifdef _POSIX_JOB_CONTROL
	/* move CLEX to its own process group */
	save_pgid = tcgetpgrp(STDIN_FILENO);
	setpgid(clex_data.pid,clex_data.pid);
	tcsetpgrp(STDIN_FILENO,clex_data.pid);
#endif
}

/* noncanonical, no echo */
void
tty_setraw(void)
{
	tcsetattr(STDIN_FILENO,TCSAFLUSH,&raw_termios);
}

/* note: this is a cleanup function */
void
tty_reset(void)
{
	if (ptermios)
		tcsetattr(STDIN_FILENO,TCSAFLUSH,ptermios);
}

/* note: this is a cleanup function */
void
tty_pgrp_reset(void)
{
#ifdef _POSIX_JOB_CONTROL
	if (save_pgid)
		tcsetpgrp(STDIN_FILENO,save_pgid);
#endif
}

void
tty_press_enter(void)
{
	int in, flags;

	fputs("Press <enter> to continue. ",stdout);
	fflush(stdout);
	tty_setraw();
	while ((in = getchar()) != '\n' && in != '\r') {
		if (in != EOF)
			continue;
		if (errno == EINTR)
			continue;
		if (errno == EAGAIN) {
			flags = fcntl(STDIN_FILENO,F_GETFL);
			if ((flags & O_NONBLOCK) == O_NONBLOCK) {
				/* clear the non-blocking flag */
				fcntl(STDIN_FILENO,F_SETFL,flags & ~O_NONBLOCK);
				continue;
			}
		}
		/* prevent looping in the case of an error */
		err_exit("Cannot read from standard input");
	}
	tty_reset();

	puts("\n----------------------------------------------");
	fflush(stdout);
}
