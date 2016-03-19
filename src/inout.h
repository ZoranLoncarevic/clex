extern void curses_initialize(void);
extern void curses_stop(void);
extern void curses_restart(void);
extern void txt_printf(const char *, ...);

extern int kbd_input(void);
extern int kbd_esc(void);
extern int kbd_getraw(void);

extern void win_frame_reconfig(void);
extern void win_layout_reconfig(void);
extern void win_bar(void);
extern void win_edit(void);
extern void win_remark_fmt(const char *, ...);
extern void win_remark(const char *);
extern void win_filter(void);
extern void win_frame(void);
extern void win_heading(void);
extern void win_panel(void);
extern void win_panel_opt(void);
extern void win_warning(const char *);
extern void win_warning_fmt(const char *, ...);
extern void win_waitmsg(void);
extern void win_completion(int, const char *);
