CLEANFILES = help.inc
EXTRA_DIST = clex.1 help_en.hlp
BUILT_SOURCES = help.inc

AM_CPPFLAGS = -DCONFIG_FILE=\"$(sysconfdir)/clexrc\"

bin_PROGRAMS = clex
clex_SOURCES = bookmarks.c bookmarks.h cfg.c cfg.h clex.h \
	completion.c completion.h control.c control.h \
	directory.c directory.h edit.c edit.h exec.c exec.h \
	filepanel.c filepanel.h filter.c filter.h help.c help.h \
	history.c history.h inout.c inout.h lang.c lang.h list.c list.h \
	match.c match.h panel.c panel.h \
	sdstring.c sdstring.h select.c select.h signals.c signals.h \
	sort.c sort.h start.c tty.c tty.h undo.c undo.h \
	userdata.c userdata.h ustring.c ustring.h util.c util.h \
	xterm_title.c xterm_title.h

# on-line help text -> C language array of structs { text, link }
# ignore comments, set VERSION and CONFIG_FILE, quote \ ' " chars
help.inc: help_en.hlp
	sed -e '/^#/d' \
	  -e 's:@VERSION\@:$(VERSION):g' \
	  -e 's:@CONFIG_FILE\@:$(sysconfdir)/clexrc:g' \
	  -e "s:\([\\'\"]\):\\\\\1:g" \
	  -e 's:\(.*\)@@=\(.*\):#"\1", "\2":' \
	  -e '/^#/!s:\(.*\):#"\1", 0:' \
	  -e 's: *":":g' \
	  -e 's:#\(.*\):{ \1 },:' help_en.hlp > help.inc

### clex manual page ###

man_MANS = clex.1
