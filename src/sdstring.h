#define SD_INIT(X)	do { (X).SDname = 0; (X).SDmem[0] = '\0';} while (0)
#define PSDSTR(X)	((X)->SDname ? (X)->SDname : (X)->SDmem)
#define  SDSTR(X)	((X).SDname  ? (X).SDname  : (X).SDmem)

extern void sd_copy(SDSTRING *,  const char *);
extern void sd_copyn(SDSTRING *, const char *, size_t);
extern void sd_reset(SDSTRING *);
