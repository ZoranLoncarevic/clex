extern void exec_initialize(void);
extern void exec_shell_reconfig(void);
extern void exec_prompt_reconfig(void);
extern void exec_nplist_reconfig(void);
extern int execute_cmd(const char *, FLAG prompt_user);

/* values for prompt_user argument in execute_cmd()  */
#define DONOT_PROMPT_USER 0
#define       PROMPT_USER 1
