#include <yed/plugin.h>

void vim_quit(int n_args, char **args);

static char EOS = '\0';
static array_t _number;

static array_t _cmd;
static array_t _cmd_history;
static yed_cmd_line_readline_ptr_t _cmd_readline;

void
vim_interactive_mode_start ()
{
    ys->interactive_command = "vim-command";
    ys->cmd_prompt = ":";

    array_clear(_cmd);
    yed_clear_cmd_buff();
    yed_cmd_line_readline_reset(_cmd_readline, &_cmd_history);
}

void
vim_interactive_mode_build_cmd (char key)
{
    yed_cmd_line_readline_take_key(_cmd_readline, key);
    array_zero_term(ys->cmd_buff);

    char *s = array_data(ys->cmd_buff);
    array_clear(_cmd);
    for (int i = 0; i <= array_len(ys->cmd_buff); i++) {
        array_grow_if_needed(_cmd);
        array_push(_cmd, s[i]);
    }
}

void
vim_interactive_mode_finish ()
{
    ys->interactive_command = NULL;
    yed_clear_cmd_buff();
}

void
vim_command (int n_args, char **args)
{
    static int is_running = 0;
    int key;

    /* vim-command is always interactive */
    if (!is_running) {
        if (n_args > 0) {
            yed_cerr("Was given arguments when expected none!");
            return;
        }
        vim_interactive_mode_start();
        is_running = 1;
        return;
    }

    /* handle each key entered in interactive mode */
    sscanf(args[0], "%d", &key);
    switch (key) {
        case ESC:
        case CTRL_C:
            vim_interactive_mode_finish();
            return;

        case ENTER:
            vim_interactive_mode_finish();
            break;

        default:
            vim_interactive_mode_build_cmd(key);
            return;
    }

    /* won't get here unless interactive mode finishes */
    is_running = 0;
    YEXE(array_data(_cmd));
}

void
vim_internal_take_key (int key)
{
    int number;

    if (is_digit(key)) {
        array_push(_number, key);
        return;
    }
    else if (array_len(_number) > 0) {
        array_push(_number, EOS);
        sscanf(array_data(_number), "%d", &number);
        yed_log("Got number: `%d`\n", number);
        array_clear(_number);
    }

    switch (key) {
        case ESC:
            /* cancel command */
            break;

        case ':':
            YEXE("vim-command");
            break;

        default:
            yed_cerr("Unhandled key %d", key);
    }
}

void
vim_take_key (int n_args, char **args)
{
    int key;
    if (n_args != 1) {
        yed_cerr("expected 1 argument, but got %d", n_args);
        return;
    }
    sscanf(args[0], "%d", &key);
    vim_internal_take_key(key);
}

int
yed_plugin_boot (yed_plugin *self)
{
    char  key_str[32];
    int key;
    YED_PLUG_VERSION_CHECK();

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
