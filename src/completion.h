#define COMPL_TYPE_AUTO		0	/* autodetect */
#define COMPL_TYPE_DIRPANEL	1	/* whole textline is one directory name */
#define COMPL_TYPE_FILE		2	/* any file */
#define COMPL_TYPE_DIR		3	/* directory */
#define COMPL_TYPE_CMD		4	/* executable */
#define COMPL_TYPE_USER		5	/* username */
#define COMPL_TYPE_ENV		6	/* environment variable */
#define COMPL_TYPE_HIST		7	/* command history */

extern void completion_initialize(void);
extern void completion_reconfig(void);
extern void compl_prepare(void);
extern int  compl_file(int);
extern void cx_compl_complete(void);
extern void cx_complete_auto(void);
extern void cx_complete_file(void);
extern void cx_complete_dir(void);
extern void cx_complete_cmd(void);
extern void cx_complete_user(void);
extern void cx_complete_env(void);
extern void cx_complete_hist(void);
