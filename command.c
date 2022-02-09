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
