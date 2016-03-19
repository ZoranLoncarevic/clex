extern void edit_update(void);
extern void edit_update_cursor(void);
extern int edit_adjust(void);

extern int edit_islong(void);
extern int edit_isspecial(int);

extern void edit_nu_insertchar(int);
extern void edit_insertchar(int);
extern void edit_nu_insertstr(const char *, int);
extern void edit_insertstr(const char *, int);
extern void edit_nu_putstr(const char *);
extern void edit_putstr(const char *);
extern void edit_macro(const char *);
extern void edit_nu_kill(void);
extern void edit_setprompt(TEXTLINE *, const char *);

extern void cx_edit_begin(void);
extern void cx_edit_end(void);
extern void cx_edit_left(void);
extern void cx_edit_right(void);
extern void cx_edit_up(void);
extern void cx_edit_down(void);
extern void cx_edit_w_left(void);
extern void cx_edit_w_left_(void);	/* transition !! */
extern void cx_edit_w_right(void);
extern void cx_edit_backsp(void);
extern void cx_edit_delchar(void);
extern void cx_edit_delend(void);
extern void cx_edit_w_del(void);
extern void cx_edit_w_del_(void);	/* transition !! */
extern void cx_edit_kill(void);
extern void cx_edit_insert_spc(void);
extern void cx_edit_cmd_f2(void);
extern void cx_edit_cmd_f3(void);
extern void cx_edit_cmd_f4(void);
extern void cx_edit_cmd_f5(void);
extern void cx_edit_cmd_f6(void);
extern void cx_edit_cmd_f7(void);
extern void cx_edit_cmd_f8(void);
extern void cx_edit_cmd_f9(void);
extern void cx_edit_cmd_f10(void);
extern void cx_edit_cmd_f11(void);
extern void cx_edit_cmd_f12(void);
extern void cx_edit_paste_dir(void);
extern void cx_edit_paste_link(void);
extern void cx_edit_filename(void);
extern void cx_edit_fullpath(void);

extern void cx_insert_filename(void);
extern void cx_insert_filenames(void);
extern void cx_insert_fullpath(void);
extern void cx_insert_d1(void);
extern void cx_insert_d2(void);
extern void cx_insert_link(void);
