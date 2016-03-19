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
#include <signal.h>		/* sigaction() */
#include <unistd.h>		/* _POSIX_JOB_CONTROL */

#include "clex.h"
#include "signals.h"

#include "control.h"	/* err_exit() */

static RETSIGTYPE
int_handler(int sn)
{
	err_exit("Signal %s caught", sn == SIGTERM ? "SIGTERM" : "SIGHUP");
}

void
signal_initialize(void)
{
	struct sigaction act;

	/* ignore keyboard generated signals */
	act.sa_handler = SIG_IGN;
	act.sa_flags = 0;
	sigemptyset(&act.sa_mask);
	sigaction(SIGINT,&act,0);
	sigaction(SIGQUIT,&act,0);

#ifdef _POSIX_JOB_CONTROL
	/* ignore job control signals */
	sigaction(SIGTSTP,&act,0);
	sigaction(SIGTTIN,&act,0);
	sigaction(SIGTTOU,&act,0);
#endif

	/* catch termination signals */
	act.sa_handler = int_handler;
	sigaddset(&act.sa_mask,SIGTERM);
	sigaddset(&act.sa_mask,SIGHUP);
	sigaction(SIGTERM,&act,0);
	sigaction(SIGHUP,&act,0);
}
