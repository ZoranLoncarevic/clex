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

#include <sys/types.h>			/* clex.h */
#include <errno.h>				/* errno */
#include <stdarg.h>				/* va_list */
#include <stdlib.h>				/* free() */
#include <string.h>				/* strlen() */
#include <unistd.h>				/* getcwd() */

#include "clex.h"
#include "ustring.h"

#include "util.h"				/* emalloc() */

extern int errno;

/*
 * The USTRING structure (defined in clex.h) can store a string
 * of unlimited length. The memory is allocated dynamically.
 *
 * - to initialize (to NULL ptr value) before first use:
 *     static USTRING us = { 0, 0 };
 *       or
 *     US_INIT(ustring);
 * - to re-initialize, e.g. before deallocating dynamic USTRING:
 *     us_reset();
 * - to store a string
 *   (NULL ptr cannot be stored, but see us_copy() function below):
 *     us_copy();
 *       or
 *     us_copyn();
 * - to retrieve a string:
 *     USTR(us)
 *       or
 *     PUSTR(pus)
 * - to edit stored name:
 *     a) allocate enough memory with us_setsize() or us_resize()
 *     b) edit the string starting at USTR() location
 *
 * WARNING: US_INIT, USTR, and PUSTR are macros
 */

/* these are tunable parameters */
#define ALLOC_UNIT			24	/* in bytes */
#define MINIMUM_FREE		(4 * ALLOC_UNIT)

/*
 * SHOULD_CHANGE_ALLOC() is true if
 *  1) we need more memory, or
 *  2) we can free considerable amount of memory (MINIMUM_FREE)
 */
#define SHOULD_CHANGE_ALLOC(RQ)	\
	(pustr->USalloc < RQ || pustr->USalloc >= RQ + MINIMUM_FREE)

/* memory is allocated in chunks to prevent excessive resizing */
#define ROUND_ALLOC(RQ)	\
	((1 + (RQ - 1) / ALLOC_UNIT) * ALLOC_UNIT)
/* note that ROUND_ALLOC(0) is ALLOC_UNIT and not 0 */

void
us_reset(USTRING *pustr)
{
	if (pustr->USalloc) {
		free(pustr->USstr);
		pustr->USalloc = 0;
	}
	pustr->USstr = 0;
}

/* us_setsize() makes room for 'memreq' characters */
void
us_setsize(USTRING *pustr, size_t memreq)
{
	if (SHOULD_CHANGE_ALLOC(memreq)) {
		if (pustr->USalloc)
			free(pustr->USstr);
		pustr->USalloc = ROUND_ALLOC(memreq);
		pustr->USstr = emalloc(pustr->USalloc);
	}
}

/* like us_setsize(), but preserving contents */
void
us_resize(USTRING *pustr, size_t memreq)
{
	if (SHOULD_CHANGE_ALLOC(memreq)) {
		pustr->USalloc = ROUND_ALLOC(memreq);
		pustr->USstr = erealloc(pustr->USstr,pustr->USalloc);
	}
}

/* quick alternative to copy */
void
us_xchg(USTRING *s1, USTRING *s2)
{
	char *xstr;
	size_t xalloc;

	xstr      = s1->USstr;
	s1->USstr = s2->USstr;
	s2->USstr = xstr;

	xalloc      = s1->USalloc;
	s1->USalloc = s2->USalloc;
	s2->USalloc = xalloc;
}

void
us_copy(USTRING *pustr, const char *src)
{
#if 0
/* uncomment this code to enable NULL ptr assignment */
	if (src == 0) {
		us_reset(pustr);
		return;
	}
#endif
	us_setsize(pustr,strlen(src) + 1);
	strcpy(pustr->USstr,src);
}

/* note: us_copyn() adds terminating null byte */
void
us_copyn(USTRING *pustr, const char *src, size_t len)
{
	char *dst;

	us_setsize(pustr,len + 1);
	dst = pustr->USstr;
	dst[len] = '\0';
	while (len-- > 0)
		dst[len] = src[len];
}

/* concatenation: us_cat(&ustring, str1, str2, ..., strN, (char *)0); */
void
us_cat(USTRING *pustr, ...)
{
	size_t len;
	char *str;
	va_list argptr;

	va_start(argptr,pustr);
	for (len = 1; (str = va_arg(argptr, char *)); )
		len += strlen(str);
	va_end(argptr);
	us_setsize(pustr,len);

	va_start(argptr,pustr);
	for (len = 0; (str = va_arg(argptr, char *)); ) {
		strcpy(pustr->USstr + len,str);
		len += strlen(str);
	}
	va_end(argptr);
}

/* USTRING version of getcwd() */
int
get_cwd_us(USTRING *pustr)
{
	us_setsize(pustr,ALLOC_UNIT);
	for (;/* until return*/;) {
		if (getcwd(pustr->USstr,pustr->USalloc))
			return 0;
		if (errno != ERANGE)
			return -1;
		/* increase buffer */
		us_setsize(pustr,pustr->USalloc + ALLOC_UNIT);
	}
}

/* USTRING version of readlink() */
int
get_link_us(USTRING *pustr, const char *path)
{
#ifndef HAVE_READLINK
	return -1;
#else
	int len;

	us_setsize(pustr,ALLOC_UNIT);
	for (;/* until return*/;) {
		len = readlink(path,pustr->USstr,pustr->USalloc);
		if (len == -1)
			return -1;
		if (len < pustr->USalloc) {
			pustr->USstr[len] = '\0';
			return 0;
		}
		/* increase buffer */
		us_setsize(pustr,pustr->USalloc + ALLOC_UNIT);
	}
#endif
}
