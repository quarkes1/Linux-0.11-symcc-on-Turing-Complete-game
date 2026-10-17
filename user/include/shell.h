#ifndef SP_SHELL_H
#define SP_SHELL_H
#define SH_COMMANDS 16
enum { SH_ALWAYS, SH_AND, SH_OR };
typedef struct {
    char *argv[33];int argc,when,append,here_fd;
    char *input,*output,*delimiter;
} ShCommand;
typedef struct { ShCommand command[SH_COMMANDS];int count; } ShList;
int sh_parse(char *,char *,int,ShList *);
#endif
