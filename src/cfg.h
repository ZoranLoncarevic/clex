extern void config_initialize(void);
extern void config_prepare(void);
extern void config_edit_num_prepare(void);
extern void config_edit_str_prepare(void);
extern const char *config_print_value(int);
extern void cx_config_enter(void);
extern void cx_config_num_enter(void);
extern void cx_config_str_enter(void);
extern void cx_config_default(void);
extern void cx_config_original(void);
extern void cx_config_save(void);
extern void cx_config_admin_save(void);

#define config_num(X) (*(const int *)pcfg[X])
#define config_str(X) ((const char *)pcfg[X])
#define config_layout \
	((const char *)pcfg[CFG_LAYOUT1 + config_num(CFG_LAYOUT)])
