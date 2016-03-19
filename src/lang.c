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

#include <sys/types.h>		/* time_t */

#include <ctype.h>			/* isalpha */
#include <string.h>			/* strchr() */
#include <time.h>			/* strftime() */

/* setlocale() */
#ifdef HAVE_LOCALE_H
# include <locale.h>
#endif

#include "clex.h"
#include "lang.h"

#include "inout.h"			/* txt_printf() */

static char sep000 = '.';				/* thousands separator */
static const char *fmt_date = "dMy";	/* format string for date */
static int clock24 = 1;					/* 12 or 24 hour clock */

void
lang_initialize(void)
{
#if defined(HAVE_SETLOCALE) && defined(HAVE_STRFTIME)
	FLAG errd;
	char *in, *out, teststr[32];
	static char result[16];
	struct lconv *lc;
	time_t testtime;

	testtime = 2916000; /* 03-feb-1970 18:00 */

	if (setlocale(LC_ALL,"") == 0)
		txt_printf("LOCALE: cannot set program's locale\n");

	/* thousands separator - dot or comma */
	lc = localeconv();
	sep000 = lc->thousands_sep[0];
	if (sep000 != ',' && sep000 != '.')
		/* the opposite of the decimal point */
		sep000 = lc->decimal_point[0] == ',' ? '.' : ','; 

	/* 12/24 hour clock */
	if (strftime(teststr,sizeof(teststr),"%X",gmtime(&testtime)) == 0)
		txt_printf("LOCALE: unable to autodetect time format,"
		  " using 24 hour clock");
	else
		/* it is either 18:00 or 06:00 PM */
		clock24 = strchr(teststr,'6') == 0;

	/* date format */
	errd = 0;
	if (strftime(teststr,sizeof(teststr),"%x",gmtime(&testtime)) == 0)
		errd = 1;
	else
		for (in = teststr, out = result; /* until break */;) {
			if (strncmp(in,"03",2) == 0) {
				in += 2;
				*out++ = 'd';
			} else if (strncmp(in,"3",1) == 0) {
				in += 1;
				*out++ = 'D';
			} else if (strncmp(in,"02",2) == 0) {
				in += 2;
				*out++ = 'm';
			} else if (strncmp(in,"1970",4) == 0) {
				in += 4;
				*out++ = 'Y';
			} else if (strncmp(in,"70",2) == 0) {
				in += 2;
				*out++ = 'y';
			} else if (isalpha((unsigned char)*in)) {
				/*
				 * assuming this is month in written form
				 * e.g. feb, February, or even some other language
				 */
				while (isalpha((unsigned char)*++in))
					;
				*out++ = 'M';
			} else {
				/* assuming punctuation */
				if ( (*out++ = *in++) == '\0') {
					fmt_date = result;
					break;	/* finished */
				}
			}
			if (out >= result + sizeof(result)) {
				errd = 1;
				break;	/* output buffer overflow */
			}
		}

		if (errd)
			txt_printf("LOCALE: unable to autodetect date format,"
			  " using \"dMy\" format");
#endif
}

int
lang_sep000(void)
{
	return sep000;
}

int
lang_clock24(void)
{
	return clock24;
}

const char *
lang_fmt_date(void)
{
	return fmt_date;
}
