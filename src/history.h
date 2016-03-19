#define hist_initialize hist_reconfig
extern void hist_reconfig(void);
extern void hist_prepare(void);
extern void hist_panel(void);
extern void hist_save(const char *, int);
extern void hist_reset_index(void);
extern const HIST_ENTRY *get_history_entry(int i);
extern void cx_hist_prev(void);
extern void cx_hist_next(void);
extern void cx_hist_paste(void);
extern void cx_hist_enter(void);
extern void cx_hist_del(void);
