#define US_INIT(X)	do { (X).USstr = 0; (X).USalloc = 0; } while (0)
#define PUSTR(X)	((X)->USstr)
#define  USTR(X)	((X).USstr)

extern void us_setsize(USTRING *, size_t);
extern void us_resize(USTRING *, size_t);
extern void us_xchg(USTRING *, USTRING *);
extern void us_copy(USTRING *, const char *);
extern void us_copyn(USTRING *, const char *, size_t);
extern void us_cat(USTRING *, ...);
extern void us_reset(USTRING *);
extern int get_cwd_us(USTRING *);
extern int get_link_us(USTRING *, const char *);
