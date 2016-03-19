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
#include <sys/stat.h>			/* fstat() */
#include <ctype.h>				/* tolower */
#include <fcntl.h>				/* open() */
#include <stdio.h>				/* sprintf() */
#include <stdlib.h>				/* malloc() */
#include <string.h>				/* strlen() */
#include <unistd.h>				/* read() */
#include <limits.h>				/* SSIZE_MAX */

#include "clex.h"
#include "util.h"

#include "control.h"			/* err_exit() */
#include "ustring.h"			/* us_resize() */

/* variables used in pathname_xxx() functions */
static USTRING sf_buff = { 0,0 };
static size_t sf_dirlen;

/* get rid of directory part in 'pathname' */
const char *
base_name(const char *pathname)
{
	const char *base, *pch;
	char ch;

	for (pch = base = pathname; (ch = *pch++) != '\0'; )
		if (ch == '/')
			base = pch;
	return base;
}

int
substring(const char *haystack, const char *needle, int ic)
{
	const char *h, *n;

	for ( ; ; haystack++ ) {
		for (h = haystack, n = needle; ; h++, n++) {
			if (*n == '\0')
				return 1;
			if (*h == '\0')
				return 0;
			if (ic ? tolower((unsigned char)*h) != tolower((unsigned char)*n)
			  : *h != *n)
				break;
		}
	}
	/* NOTREACHED */
	return 0;
}

static void
alloc_fail(size_t size)
{
	err_exit("Memory allocation failed, could not allocate %lu bytes",
	  (unsigned long)size);
	/* NOTREACHED */
}

/* malloc with error checking */
void *
emalloc(size_t size)
{
	void *mem;

	if (size > SSIZE_MAX)
		/*
		 * possible problems with signed/unsigned int !
		 *
		 * It is not normal to request such a huge memory
		 * block anyway (16-bit systems are not supported)
		 */
		alloc_fail(size);
	if ((mem = malloc(size)) == 0)
		alloc_fail(size);
	return mem;
}

/* realloc with error checking */
void *
erealloc(void *ptr, size_t size)
{
	void *mem;

	/* not sure if really all realloc()s can handle this case */
	if (ptr == 0)
		return emalloc(size);

	if (size > SSIZE_MAX)
		/* see emalloc() above */
		alloc_fail(size);

	if ((mem = realloc(ptr,size)) == 0)
		alloc_fail(size);
	return mem;
}

/* strdup with error checking */
char *
estrdup(const char *str)
{
	char *dup;

	if (str == 0)
		return 0;
	dup = emalloc(strlen(str) + 1);
	strcpy(dup,str);
	return dup;
}

/* set the directory name for pathname_join() */
void
pathname_set_directory(const char *dir)
{
	char *str;

	sf_dirlen = strlen(dir);
	us_resize(&sf_buff,sf_dirlen + 24);
	/* 24 extra bytes = slash + initial space for the filename */
	str = USTR(sf_buff);
	strcpy(str,dir);
	if (str[sf_dirlen - 1] != '/')
		str[sf_dirlen++] = '/';
		/* the string is not null terminated now, that's ok */
}

/*
 * join the filename 'file' with the directory set by
 * pathname_set_directory() above
 *
 * returned data is overwritten by subsequent calls
 */
char *
pathname_join(const char *file)
{
	us_resize(&sf_buff,sf_dirlen + strlen(file) + 1);
	strcpy(USTR(sf_buff) + sf_dirlen,file);
	return USTR(sf_buff);
}

char *
my_strerror(int errcode)
{
	static char buffer[32];

	sprintf(buffer,"errno = %d",errcode);
	return buffer;
}

/*
 * dequote backslash quoted text: 'src' -> 'dst'
 * 'dst' buffer must be large enough
 */
size_t
dequote_txt(const char *src, size_t len, char *dst)
{
	char ch;
	size_t i, j;
	FLAG quote;

	for (i = j = 0, quote = 0; i < len; i++) {
		ch = src[i];
		if (TCLR(quote) || !(quote = ch == '\\'))
			dst[j++] = ch;
	}
	dst[j] = '\0';

	return j;
}

/*
 * under certain condition can read() return fewer bytes than requested,
 * this wrapper function handles it
 *
 * remember: error check should be (read() == -1) and not (read() < 0)
 */
ssize_t
read_fd(int fd, char *buff, size_t bytes)
{
	size_t total;
	ssize_t rd;

	for (total = 0; bytes > 0; total += rd, bytes -= rd) {
		rd = read(fd,buff + total,bytes);
		if (rd == -1)	/* error */
			return -1;
		if (rd == 0)	/* EOF */
			break;
	}
	return total;
}

/*
 * read file into memory
 * input: filename, *size = max size
 * output (success): return value = allocated buffer, *size = actual size
 * output (failure): return value = NULL, *errcode = error code
 */
char *
read_file(const char *filename, size_t *size, int *errcode)
{
	int fd;
	struct stat stbuf;
	size_t filesize;	/* we do not need off_t here */
	ssize_t rd;
	char *buffer;

	if ( (fd = open(filename,O_RDONLY)) < 0) {
		*errcode = 1;	/* cannot open (errno is set) */
		return 0;
	}

	fstat(fd,&stbuf);	/* cannot fail with valid descriptor */
	if (stbuf.st_size > *size) {
		close(fd);
		*errcode = 2;	/* size too big */
		return 0;
	}

	/* read into memory */
	filesize = stbuf.st_size;
	buffer = emalloc(filesize + 1);
	rd = filesize ? read_fd(fd,buffer,filesize) : 0;
	close(fd);

	if (rd == -1) {
		free(buffer);
		*errcode = 3;	/* read error (errno is set) */
		return 0;
	}

	if (filesize && buffer[filesize - 1] != '\n')
		buffer[filesize++] = '\n';

	*size = filesize;
	return buffer;
}

time_t
mod_time(const char *file)
{
	struct stat stbuf;

	return stat(file,&stbuf) < 0 ? 0 : stbuf.st_mtime;
}
