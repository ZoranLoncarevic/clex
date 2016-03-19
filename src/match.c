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

/* match.c implements shell regular expression matching */

#include <config.h>

#include <sys/types.h>	/* clex.h */
#include <string.h>		/* strlen() */

#include "clex.h"
#include "match.h"

#include "inout.h"		/* win_warning() */
#include "ustring.h"	/* us_setsize() */

static const char *expr;	/* shell regular expression */
static char *sym;			/* corresponding string of SYM_XXX */
static size_t expr_len;		/* length of regular expression */

/* types of symbols in shell regular expression */
#define SYM_LITERAL		 0	/* quoted or no special meaning */
#define SYM_IGNORE		 1	/* ignore this char (e.g. quoting) */
#define SYM_ANY_CHAR	 2	/* question mark ? metacharacter */
#define SYM_ANY_STRING	 3	/* asterisk * metacharacter */
#define SYM_LIST_BEGIN	 4	/* list begin [ metacharacter */
#define SYM_LIST_END	 5	/* list end ] metacharacter */
#define SYM_LIST_INV	 6	/* either ^ or ! after SYM_LIST_BEGIN */
#define SYM_RANGE_FROM	 7	/* lower limit in a range a-z */
#define SYM_RANGE_TO	 8	/* upper limit in a range a-z */
/* temporary values used during parsing process */
#define SYM_TEMP_HYPHEN	98	/* hyphen in a list, needs further checks */
#define SYM_TEMP_NQ		99	/* not quoted, needs further parsing */

/* finite state machine to parse quoting characters */
#define STATE_NORMAL			0	/* not quoted */
#define STATE_BACKSLASH			1	/* quoting backslash */
#define STATE_SINGLE			2	/* inside single quotes */
#define STATE_DOUBLE			3	/* inside double quotes */
#define STATE_DOUBLE_BACKSLASH	4	/* backslash inside double quotes */

/* finite state machine to parse metacharacters */
/* STATE_NORMAL shared */
#define STATE_LIST_BEGIN		1	/* [ encountered */
#define STATE_LIST_BEGIN_2		2	/* [^ or [! case */
#define STATE_LIST_BODY			3	/* between [ and ] */

/* finite state machine to parse ranges a-z */
/* STATE_NORMAL shared */
#define STATE_LIST				1	/* between [ and ] */
#define STATE_RANGE1			2	/* after a  (looking for a-z) */
#define STATE_RANGE2			3	/* after a- (looking for a-z) */

/* match result - used in check_match() only */
#define MATCH_NO				1
#define MATCH_YES				2
#define MATCH_NEVER				3

/* errors in regular expression */
#define ERR_NONE				0
#define ERR_UNBALANCED			1
#define ERR_MISSING_SINGLE		2
#define ERR_MISSING_DOUBLE		3
#define ERR_BACKSLASH			4

#define TRANS(SYM,STAT) \
  do { sym[i] = (SYM_ ## SYM); state = (STATE_ ## STAT); } while (0)
#define SYM(SYM) \
  do { sym[i] = (SYM_ ## SYM); /* state unchanged */     } while (0)
#define STATE(STAT) \
  do { /* symbol unchanged */  state = (STATE_ ## STAT); } while (0)

static int
rangecmp(int from, int ch, int to)
{
	static char f[2] = "?", c[2] = "?", t[2] = "?";

	f[0] = (char)from;
	c[0] = (char)ch;
	t[0] = (char)to;
	return STRCOLL(f,c) <= 0 && STRCOLL(c,t) <= 0;
}

/*
 * check if shell regular expression set by last successful check_sre()
 * call and starting at position 'i' matches the string 'word'
 */
static int
check_match(size_t i, const char *word)
{
	int m, from;
	FLAG inlist, inverse, listmatch;
	char ch;

	from = 0; ch = '\0';		/* to prevent compiler warning */
	inverse = listmatch = 0;	/* to prevent compiler warning */
	inlist = 0;					/* not inside [list] */

	for (; i < expr_len; i++)
		switch (sym[i]) {
		case SYM_IGNORE:
			break;
		case SYM_LITERAL:
			if (inlist) {
				if (expr[i] == ch)
					listmatch = 1;
			}
			else if (expr[i] != *word++)
				return MATCH_NO;
			break;
		case SYM_ANY_CHAR:
			if (*word++ == '\0')
				return MATCH_NO;
			break;
		case SYM_ANY_STRING:
			if (i + 1 == expr_len)
				/* shortcut: single * matches it all */
				return MATCH_YES;
			do {
				if ( (m = check_match(i + 1,word)) != MATCH_NO)
					return m;
			} while (*word++);
			/*
			 * match impossible, stop recursion, otherwise you can
			 * spend many hours here if there is lots of asterisks
			 */
			return MATCH_NEVER;
		case SYM_LIST_BEGIN:
			inlist = 1;
			inverse = listmatch = 0;
			if ((ch = *word++) == '\0')
				return MATCH_NO;
			break;
		case SYM_LIST_INV:
			inverse = 1;
			break;
		case SYM_RANGE_FROM:
			from = expr[i];
			break;
		case SYM_RANGE_TO:
			if (rangecmp(from,ch,expr[i]))
				listmatch = 1;
			break;
		case SYM_LIST_END:
			if (listmatch == inverse)
				return MATCH_NO;
			inlist = 0;
			break;
		}

	return *word == '\0' ? MATCH_YES : MATCH_NO ;
}

static int
parse_expression(void)
{
	char ch;
	int	state, symbol, from, hyphen;
	size_t i;

	/*
	 * handle quoting: this finite state machine recognizes:
	 *   - special quoting characters (tagged as SYM_IGNORE)
	 *   - quoted text (SYM_LITERAL)
	 * everything else is left tagged as SYM_TEMP_NQ
	 */
	for (state = STATE_NORMAL, i = 0; i < expr_len; i++) {
		ch = expr[i];
		switch (state) {
		case STATE_NORMAL:
			if (ch == '\\')
				TRANS(IGNORE,BACKSLASH);
			else if (ch == '\'')
				TRANS(IGNORE,SINGLE);
			else if (ch == '\"')
				TRANS(IGNORE,DOUBLE);
			else
				SYM(TEMP_NQ);
			break;
		case STATE_BACKSLASH:
			TRANS(LITERAL,NORMAL);
			break;
		case STATE_DOUBLE_BACKSLASH:
			TRANS(LITERAL,DOUBLE);
			break;
		case STATE_SINGLE:
			if (ch == '\'')
				TRANS(IGNORE,NORMAL);
			else
				SYM(LITERAL);
			break;
		case STATE_DOUBLE:
			if (ch == '\\')
				/* backslash quotes only \ " and $ */
				if (strchr("\\\"$",(unsigned char)expr[i + 1]))
					TRANS(IGNORE,DOUBLE_BACKSLASH);
				else
					SYM(LITERAL);
			else if (ch == '\"')
				TRANS(IGNORE,NORMAL);
			else
				SYM(LITERAL);
		}
	}
	if (state == STATE_SINGLE)
		return ERR_MISSING_SINGLE;
	if (state == STATE_DOUBLE || state == STATE_DOUBLE_BACKSLASH)
		return ERR_MISSING_DOUBLE;
	if (state == STATE_BACKSLASH)
		return ERR_BACKSLASH;

	/*
	 * handle metacharacters: ?, *, and [list]
	 * this finite state machine removes all occurrences of temporary
	 * SYM_TEMP_NQ, it generates new symbols:
	 *   - SYM_ANY_CHAR and SYM_ANY_STRING
	 *   - SYM_LIST_BEGIN and SYM_LIST_END
	 *   - temporary SYM_TEMP_HYPHEN
	 */
	for (state = STATE_NORMAL, i = 0; i < expr_len; i++) {
		symbol = sym[i];
		if (symbol == SYM_IGNORE)
			continue;
		ch = expr[i];
		switch (state) {
		case STATE_NORMAL:
			if (symbol == SYM_LITERAL)
				break;
			if (ch == '[')
				TRANS(LIST_BEGIN,LIST_BEGIN);
			else if (ch == '?')
				SYM(ANY_CHAR);
			else if (ch == '*')
				SYM(ANY_STRING);
			else	/* sym == SYM_TEMP_NQ, i.e. unquoted character */
				SYM(LITERAL);
			break;
		case STATE_LIST_BEGIN:
			if (symbol == SYM_TEMP_NQ && (ch == '^' || ch == '!'))
				TRANS(LIST_INV,LIST_BEGIN_2);
			else
				TRANS(LITERAL,LIST_BODY);
			break;
		case STATE_LIST_BEGIN_2:
			TRANS(LITERAL,LIST_BODY);
			break;
		case STATE_LIST_BODY:
			if (symbol == SYM_TEMP_NQ && ch == ']')
				TRANS(LIST_END,NORMAL);
			else if (symbol == SYM_TEMP_NQ && ch == '-')
				SYM(TEMP_HYPHEN);
			else
				SYM(LITERAL);
		}
	}
	if (state != STATE_NORMAL)
		return ERR_UNBALANCED;

	/*
	 * handle ranges a-z in [list]
	 * if SYM_TEMP_HYPHEN is part of a valid range, this range will
	 * be tagged with SYM_RANGE_FROM and SYM_RANGE_TO
	 */
	from = hyphen = 0; 	/* to prevent compiler warning */
	for (state = STATE_NORMAL, i = 0; i < expr_len; i++) {
		symbol = sym[i];
		if (symbol == SYM_IGNORE)
			continue;
		switch (state) {
		case STATE_NORMAL:
			if (symbol == SYM_LIST_BEGIN)
				STATE(LIST);
			break;
		case STATE_LIST:
			/* inside [...] */
			if (symbol == SYM_LIST_END)
				STATE(NORMAL);
			else if (symbol == SYM_LITERAL) {
				from = i;
				STATE(RANGE1);
			}
			else if (symbol == SYM_TEMP_HYPHEN)
				/* e.g. second hyphen in a list like [a-z-_] */
				sym[i] = SYM_LITERAL;
			break;
		case STATE_RANGE1:
			if (symbol == SYM_LIST_END)
				STATE(NORMAL);
			else if (symbol == SYM_LITERAL)
				from = i;
			else if (symbol == SYM_TEMP_HYPHEN) {
				hyphen = i;
				STATE(RANGE2);
			}
			break;
		case STATE_RANGE2:
			if (symbol == SYM_LIST_END) {
				sym[hyphen] = SYM_LITERAL;
				STATE(NORMAL);
			}
			else {
				/* valid range [a-z] */
				sym[from] = SYM_RANGE_FROM;
				sym[hyphen] = SYM_IGNORE;
				sym[i] = SYM_RANGE_TO;
				STATE(LIST);
			}
		}
	}

	return ERR_NONE;
}

int
match(const char *word)
{
	size_t i;
	FLAG dot_match;

	if (*word == '.') {
		/* exception: only literal dot matches */
		for (dot_match = 0, i = 0; i < expr_len; i++) {
			if (sym[i] == SYM_IGNORE)
				continue;
			if (sym[i] == SYM_LITERAL && expr[i] == '.')
				dot_match = 1;
			break;
		}
		if (!dot_match)
			return 0;
	}
	return check_match(0,word) == MATCH_YES;
}

int
check_sre(const char *sre)
{
	static USTRING symbols = { 0,0 };

	expr = sre;
	expr_len = strlen(expr);
	us_setsize(&symbols,expr_len);
	sym = USTR(symbols);

	return parse_expression();
}

int
match_sre(const char *sre)
{
	static char *errmsg[] = {
		0,
		"Unbalanced [ ]",
		"Missing single quote",
		"Missing double quote",
		"Misplaced backslash"
	};	/* must match #ERR_XXX defines */
	int code;

	if ((code = check_sre(sre)) == ERR_NONE)
		return 0;
	win_warning_fmt("SYNTAX CHECK: %s.",errmsg[code]);
	return -1;
}
