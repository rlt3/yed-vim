#include <yed/plugin.h>

void vim_quit(int n_args, char **args);

static char EOS = '\0';
static array_t _number;

static array_t _cmd;
static array_t _cmd_history;
static yed_cmd_line_readline_ptr_t _cmd_readline;

#include "tokens.c"
#include "parse.c"
#include "command.c"

void
vim_take_key (int n_args, char **args)
{
    int key;
    if (n_args != 1) {
        yed_cerr("expected 1 argument, but got %d", n_args);
        return;
    }
    sscanf(args[0], "%d", &key);

    tokens_push(&_tokens, key);
    expression(&_tokens);
}

int
yed_plugin_boot (yed_plugin *self)
{
    char  key_str[32];
    int key;
    YED_PLUG_VERSION_CHECK();

    tokens_make(&_tokens);

    _number = array_make(char);

    _cmd          = array_make_with_cap(char, 16);
    _cmd_history  = array_make(char*);
    _cmd_readline = malloc(sizeof(*ys->search_readline));
    yed_cmd_line_readline_make(_cmd_readline, &_cmd_history);

    yed_plugin_set_command(self, "vim-take-key", vim_take_key);
    yed_plugin_set_command(self, "vim-command", vim_command);
    for (key = 1; key < REAL_KEY_MAX; key += 1) {
        sprintf(key_str, "%d", key);
        YPBIND(self, key, "vim-take-key", key_str);
    }

    return 0;
}
