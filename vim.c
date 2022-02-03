#include <yed/plugin.h>

/*
 * TODO:
 * - x, delete single character without entering insert
 * - O, o; create a new line above or below, go to its beginning, enter insert mode
 * - V, v; visual block and visual select modes
 * - %, travel on parens or brackets
 * - $, ^; travel to end or beginning of line
 * - ctrl + E, ctrl + Y; scroll buffer down or up
 * - I; insert at beginning of line
 */

/* COMMANDS */
void vim_take_key(int n_args, char **args);
void vim_bind(int n_args, char **args);
void vim_unbind(int n_args, char **args);
void vim_exit_insert(int n_args, char **args);
void vim_write(int n_args, char **args);
void vim_quit(int n_args, char **args);
void vim_write_quit(int n_args, char **args);
/* END COMMANDS */

typedef enum Mode {
    MODE_NORMAL = 0,
    MODE_INSERT,
    MODE_DELETE,
    MODE_YANK,
    MODE_VIRTUAL,
    /* N_NODES should always be last */
    N_MODES
} Mode;

static int restore_cursor_line;

static char *mode_strs[] = {
    "NORMAL",
    "INSERT",
    "DELETE",
    "YANK",
    "VIRTUAL"
};

static char *mode_strs_lowercase[] = {
    "normal",
    "insert",
    "delete",
    "yank",
    "virtual"
};

static int vim_mode_completion(char *string, yed_completion_results *results) {
    int status;

    FN_BODY_FOR_COMPLETE_FROM_ARRAY(string, 4, mode_strs_lowercase, results, status);

    return status;
}

typedef struct {
    int    len;
    int    keys[MAX_SEQ_LEN];
    char  *cmd;
    int    key;
    int    n_args;
    char **args;
} vim_key_binding;

static yed_plugin *Self;
static Mode mode;
static array_t mode_bindings[N_MODES];
static array_t repeat_keys;
static int repeating;
static int till_pending; /* 0 = not pending, 1 = pending forward, 2 = pending backward, 3 = pending backward; stop before */
static int last_till_key;
static char last_till_op;
static int num_undo_records_before_insert;

void vim_unload(yed_plugin *self);
void vim_normal(int key, char* key_str);
void vim_insert(int key, char* key_str);
void vim_delete(int key, char* key_str);
void vim_yank(int key, char* key_str);
void bind_keys(void);
void vim_change_mode(Mode new_mode, int by_line, int cancel);
void enter_insert(void);
void exit_insert(void);
void enter_delete(int by_line);
void exit_delete(int cancel);
void enter_yank(int by_line);
void exit_yank(int cancel);
void vim_make_binding(int b_mode, int n_keys, int *keys, char *cmd, int n_args, char **args);
void vim_remove_binding(int b_mode, int n_keys, int *keys);

int yed_plugin_boot(yed_plugin *self) {
    int i;

    YED_PLUG_VERSION_CHECK();

    Self = self;

    for (i = 0; i < N_MODES; i += 1) {
        mode_bindings[i] = array_make(vim_key_binding);
    }

    repeat_keys = array_make(int);

    yed_plugin_set_unload_fn(Self, vim_unload);

    yed_plugin_set_command(Self, "vim-take-key",    vim_take_key);
    yed_plugin_set_command(Self, "vim-bind",        vim_bind);
    yed_plugin_set_command(Self, "vim-unbind",      vim_unbind);
    yed_plugin_set_command(Self, "vim-exit-insert", vim_exit_insert);
    yed_plugin_set_command(Self, "w",                  vim_write);
    yed_plugin_set_command(Self, "W",                  vim_write);
    yed_plugin_set_command(Self, "q",                  vim_quit);
    yed_plugin_set_command(Self, "Q",                  vim_quit);
    yed_plugin_set_command(Self, "wq",                 vim_write_quit);
    yed_plugin_set_command(Self, "Wq",                 vim_write_quit);

    yed_plugin_set_completion(Self, "vim-mode", vim_mode_completion);
    yed_plugin_set_completion(Self, "vim-bind-compl-arg-0", vim_mode_completion);
    yed_plugin_set_completion(Self, "vim-bind-compl-arg-2", yed_get_completion("command"));
    yed_plugin_set_completion(Self, "vim-unbind-compl-arg-0", vim_mode_completion);

    bind_keys();

    if (yed_get_var("vim-normal-attrs") == NULL) {
        yed_set_var("vim-normal-attrs", "bg !4");
    }
    if (yed_get_var("vim-insert-attrs") == NULL) {
        yed_set_var("vim-insert-attrs", "bg !2");
    }
    if (yed_get_var("vim-delete-attrs") == NULL) {
        yed_set_var("vim-delete-attrs", "bg !1");
    }
    if (yed_get_var("vim-yank-attrs") == NULL) {
        yed_set_var("vim-yank-attrs", "bg !5");
    }

    vim_change_mode(MODE_NORMAL, 0, 0);
    yed_set_var("vim-mode", mode_strs[mode]);

    return 0;
}

void vim_unload(yed_plugin *self) {
    int                 i, j;
    vim_key_binding *b;

    for (i = 0; i < N_MODES; i += 1) {
        array_traverse(mode_bindings[i], b) {
            if (b->args) {
                for (j = 0; j < b->n_args; j += 1) {
                    free(b->args[j]);
                }
                free(b->args);
            }
            free(b->cmd);
        }
        array_free(mode_bindings[i]);
    }
    array_free(repeat_keys);
}

void bind_keys(void) {
    int   meta_keys[2];
    int   meta_key;
    char *ctrl_h_is_bs;
    char *ctrl_h_is_bs_cpy;
    int   key;
    char  key_str[32];

    meta_keys[0] = ESC;

    meta_keys[1] = ARROW_UP;
    meta_key     = yed_get_key_sequence(2, meta_keys);
    yed_unbind_key(meta_key);
    yed_delete_key_sequence(meta_key);
    meta_keys[1] = ARROW_DOWN;
    meta_key     = yed_get_key_sequence(2, meta_keys);
    yed_unbind_key(meta_key);
    yed_delete_key_sequence(meta_key);
    meta_keys[1] = ARROW_RIGHT;
    meta_key     = yed_get_key_sequence(2, meta_keys);
    yed_unbind_key(meta_key);
    yed_delete_key_sequence(meta_key);
    meta_keys[1] = ARROW_LEFT;
    meta_key     = yed_get_key_sequence(2, meta_keys);
    yed_unbind_key(meta_key);
    yed_delete_key_sequence(meta_key);

    if ((ctrl_h_is_bs = yed_get_var("ctrl-h-is-backspace"))) {
        ctrl_h_is_bs_cpy = strdup(ctrl_h_is_bs);
        yed_unset_var("ctrl-h-is-backspace");
    }

    for (key = 1; key < REAL_KEY_MAX; key += 1) {
        sprintf(key_str, "%d", key);
        YPBIND(Self, key, "vim-take-key", key_str);
    }

    if (ctrl_h_is_bs) {
        yed_set_var("ctrl-h-is-backspace", ctrl_h_is_bs_cpy);
        free(ctrl_h_is_bs_cpy);
    }
}

void vim_change_mode(Mode new_mode, int by_line, int cancel) {
    char                key_str[32];
    vim_key_binding *b;

    array_traverse(mode_bindings[mode], b) {
        yed_unbind_key(b->key);
        if (b->len > 1) {
            yed_delete_key_sequence(b->key);
        } else if (b->key < REAL_KEY_MAX) {
            sprintf(key_str, "%d", b->key);
            YPBIND(Self, b->key, "vim-take-key", key_str);
        }
    }

    array_traverse(mode_bindings[new_mode], b) {
        if (b->len > 1) {
            b->key = yed_plugin_add_key_sequence(Self, b->len, b->keys);
        } else {
            b->key = b->keys[0];
        }

        yed_plugin_bind_key(Self, b->key, b->cmd, b->n_args, b->args);
    }

    switch (mode) {
        case MODE_NORMAL:                      break;
        case MODE_INSERT: exit_insert();       break;
        case MODE_DELETE: exit_delete(cancel); break;
        case MODE_YANK:   exit_yank(cancel);   break;
    }

    mode = new_mode;

    switch (new_mode) {
        case MODE_NORMAL: {
            yed_set_var("enable-search-cursor-move", "no");
            break;
        }
        case MODE_INSERT: enter_insert();        break;
        case MODE_DELETE: enter_delete(by_line); break;
        case MODE_YANK:   enter_yank(by_line);   break;
    }

    yed_set_var("vim-mode", mode_strs[new_mode]);

    yed_set_var("vim-mode-attrs", yed_get_var("vim-insert-attrs"));

    switch (new_mode) {
        case MODE_NORMAL: yed_set_var("vim-mode-attrs", yed_get_var("vim-normal-attrs")); break;
        case MODE_INSERT: yed_set_var("vim-mode-attrs", yed_get_var("vim-insert-attrs")); break;
        case MODE_DELETE: yed_set_var("vim-mode-attrs", yed_get_var("vim-delete-attrs")); break;
        case MODE_YANK:   yed_set_var("vim-mode-attrs", yed_get_var("vim-yank-attrs"));   break;
    }
}

static void _vim_take_key(int key, char *maybe_key_str) {
    char *key_str, buff[32];

    if (maybe_key_str) {
        key_str = maybe_key_str;
    } else {
        sprintf(buff, "%d", key);
        key_str = buff;
    }

    switch (mode) {
        case MODE_NORMAL: vim_normal(key, key_str); break;
        case MODE_INSERT: vim_insert(key, key_str); break;
        case MODE_DELETE: vim_delete(key, key_str); break;
        case MODE_YANK:   vim_yank(key, key_str);   break;
        default:
            LOG_FN_ENTER();
            yed_log("[!] invalid mode (?)");
            LOG_EXIT();
    }
}

void vim_take_key(int n_args, char **args) {
    int key;

    if (n_args != 1) {
        yed_cerr("expected 1 argument, but got %d", n_args);
        return;
    }

    sscanf(args[0], "%d", &key);

    _vim_take_key(key, args[0]);
}

void vim_bind(int n_args, char **args) {
    char *mode_str, *cmd;
    int   b_mode, n_keys, keys[MAX_SEQ_LEN], n_cmd_args;

    if (n_args < 1) {
        yed_cerr("missing 'mode' as first argument");
        return;
    }

    mode_str = args[0];

    if      (strcmp(mode_str, "normal") == 0)    { b_mode = MODE_NORMAL; }
    else if (strcmp(mode_str, "insert") == 0)    { b_mode = MODE_INSERT; }
    else if (strcmp(mode_str, "delete") == 0)    { b_mode = MODE_DELETE; }
    else if (strcmp(mode_str, "yank")   == 0)    { b_mode = MODE_YANK;   }
    else {
        yed_cerr("no mode named '%s'", mode_str);
        return;
    }

    if (n_args < 2) {
        yed_cerr("missing 'keys' as second argument");
        return;
    }

    if (n_args < 3) {
        yed_cerr("missing 'command', 'command_args'... as third and up arguments");
        return;
    }

    n_keys = yed_string_to_keys(args[1], keys);
    if (n_keys == -1) {
        yed_cerr("invalid string of keys '%s'", args[1]);
        return;
    }
    if (n_keys == -2) {
        yed_cerr("too many keys to be a sequence in '%s'", args[1]);
        return;
    }

    cmd = args[2];

    n_cmd_args = n_args - 3;
    vim_make_binding(b_mode, n_keys, keys, cmd, n_cmd_args, args + 3);
}

void vim_unbind(int n_args, char **args) {
    char *mode_str;
    int   b_mode, n_keys, keys[MAX_SEQ_LEN];

    if (n_args < 1) {
        yed_cerr("missing 'mode' as first argument");
        return;
    }

    mode_str = args[0];

    if      (strcmp(mode_str, "normal") == 0)    { b_mode = MODE_NORMAL; }
    else if (strcmp(mode_str, "insert") == 0)    { b_mode = MODE_INSERT; }
    else if (strcmp(mode_str, "delete") == 0)    { b_mode = MODE_DELETE; }
    else if (strcmp(mode_str, "yank")   == 0)    { b_mode = MODE_YANK;   }
    else {
        yed_cerr("no mode named '%s'", mode_str);
        return;
    }

    if (n_args != 2) {
        yed_cerr("missing 'keys' as second argument");
        return;
    }

    n_keys = yed_string_to_keys(args[1], keys);
    if (n_keys == -1) {
        yed_cerr("invalid string of keys '%s'", args[1]);
        return;
    }
    if (n_keys == -2) {
        yed_cerr("too many keys to be a sequence in '%s'", args[1]);
        return;
    }

    vim_remove_binding(b_mode, n_keys, keys);
}

void vim_make_binding(int b_mode, int n_keys, int *keys, char *cmd, int n_args, char **args) {
    int                 i;
    vim_key_binding  binding, *b;

    if (n_keys <= 0) {
        return;
    }

    binding.len = n_keys;
    for (i = 0; i < n_keys; i += 1) {
        binding.keys[i] = keys[i];
    }
    binding.cmd = strdup(cmd);
    binding.key = KEY_NULL;
    binding.n_args = n_args;
    if (n_args) {
        binding.args = malloc(sizeof(char*) * n_args);
        for (i = 0; i < n_args; i += 1) {
            binding.args[i] = strdup(args[i]);
        }
    } else {
        binding.args = NULL;
    }

    array_push(mode_bindings[b_mode], binding);

    if (b_mode == mode) {
        array_traverse(mode_bindings[mode], b) {
            yed_unbind_key(b->key);
            if (b->len > 1) {
                yed_delete_key_sequence(b->key);
            }
        }

        array_traverse(mode_bindings[mode], b) {
            if (b->len > 1) {
                b->key = yed_plugin_add_key_sequence(Self, b->len, b->keys);
            } else {
                b->key = b->keys[0];
            }

            yed_plugin_bind_key(Self, b->key, b->cmd, b->n_args, b->args);
        }
    }
}

void vim_remove_binding(int b_mode, int n_keys, int *keys) {
    int                 i;
    vim_key_binding *b;

    if (n_keys <= 0) {
        return;
    }

    i = 0;
    array_traverse(mode_bindings[b_mode], b) {
        if (b->len == n_keys
        &&  memcmp(b->keys, keys, n_keys * sizeof(int)) == 0) {
            break;
        }
        i += 1;
    }

    if (i == array_len(mode_bindings[b_mode])) { return; }

    if (b_mode == mode) {
        array_traverse(mode_bindings[mode], b) {
            yed_unbind_key(b->key);
            if (b->len > 1) {
                yed_delete_key_sequence(b->key);
            }
        }

        array_delete(mode_bindings[b_mode], i);

        array_traverse(mode_bindings[mode], b) {
            if (b->len > 1) {
                b->key = yed_plugin_add_key_sequence(Self, b->len, b->keys);
            } else {
                b->key = b->keys[0];
            }

            yed_plugin_bind_key(Self, b->key, b->cmd, b->n_args, b->args);
        }
    } else {
        array_delete(mode_bindings[b_mode], i);
    }
}

static void vim_push_repeat_key(int key) {
    if (repeating) {
        return;
    }

    array_push(repeat_keys, key);
}

static void vim_pop_repeat_key(void) {
    if (repeating) {
        return;
    }

    array_pop(repeat_keys);
}

static void vim_repeat(void) {
    int *key_it;

    repeating = 1;
    array_traverse(repeat_keys, key_it) {
        _vim_take_key(*key_it, NULL);
    }
    repeating = 0;
}

static void vim_start_repeat(int key) {
    if (repeating) {
        return;
    }

    array_clear(repeat_keys);
    array_push(repeat_keys, key);
}

static void vim_cursor_end_of_word() {
    yed_frame *f;
    yed_line  *line;
    int        col;
    int        row;
    yed_glyph *g;
    int        skip_first;

    //if (!ys->active_frame || !ys->active_frame->buffer) return;
    //f = ys->active_frame;
    //line = yed_buff_get_line(f->buffer, f->cursor_line);

    //if (yed_line_last_glyph(line) == yed_line_col_to_glyph(line, f->cursor_col)) {
    //}


    YEXE("cursor-next-word");

    if (!ys->active_frame || !ys->active_frame->buffer) return;
    f = ys->active_frame;

    if (row > f->cursor_line) {
    }

    row = f->cursor_line;
    col = f->cursor_col;
    line = yed_buff_get_line(f->buffer, row);

    while (col >= 0) {
        g = yed_line_col_to_glyph(line, col);
        if (!isspace(g->c))
            break;
        col -= yed_get_glyph_width(*g);
    }

    yed_set_cursor_within_frame(f, f->cursor_line, col);
}

void vim_exit_insert(int n_args, char **args) {
    vim_push_repeat_key(CTRL_C);
    vim_change_mode(MODE_NORMAL, 0, 0);
}

static void vim_do_till_fw(int key) {
    yed_frame *f;
    yed_line  *line;
    int        col;
    yed_glyph *g;

    if (!ys->active_frame || !ys->active_frame->buffer)    { goto out; }

    f = ys->active_frame;

    line = yed_buff_get_line(f->buffer, f->cursor_line);

    if (!line)    { goto out; }

    for (col = f->cursor_col + 1; col <= line->visual_width; ) {
        g = yed_line_col_to_glyph(line, col);
        if (g->c == key) {
            yed_set_cursor_within_frame(f, f->cursor_line, col);
            break;
        }
        col += yed_get_glyph_width(*g);
    }

    last_till_key = key;

out:
    till_pending = 0;
    return;
}

static void vim_do_till_bw(int key, int stop_before) {
    yed_frame *f;
    yed_line  *line;
    int        col;
    yed_glyph *g;

    if (!ys->active_frame || !ys->active_frame->buffer)    { goto out; }

    f = ys->active_frame;

    line = yed_buff_get_line(f->buffer, f->cursor_line);

    if (!line)    { goto out; }

    for (col = f->cursor_col - 1; col >= 1;) {
        g = yed_line_col_to_glyph(line, col);
        if (g->c == key) {
            yed_set_cursor_within_frame(f,
                                        f->cursor_line,
                                        col + (stop_before * yed_get_glyph_width(*g)));
            break;
        }
        if (col == 1) { break; } /* Didn't find it. Prevent endless loop. */
        col = yed_line_idx_to_col(line, yed_line_col_to_idx(line, col - 1));
    }

    last_till_key = key;

out:
    till_pending = 0;
    return;
}

static void vim_repeat_till(void) {
    if (last_till_op == 'f' || last_till_op == 't') {
        vim_do_till_fw(last_till_key);
    } else if (last_till_op == 'F' || last_till_op == 'T') {
        vim_do_till_bw(last_till_key, last_till_op == 'T');
    }
}

int vim_nav_common(int key, char *key_str) {
    if (till_pending == 1) {
        vim_do_till_fw(key);
        if (mode != MODE_NORMAL) {
            vim_push_repeat_key(key);
        }
        goto out;
    } else if (till_pending > 1) {
        vim_do_till_bw(key, till_pending == 3);
        if (mode != MODE_NORMAL) {
            vim_push_repeat_key(key);
        }
        goto out;
    }

    switch (key) {
        case 'h':
        case ARROW_LEFT:
            YEXE("cursor-left");
            break;

        case 'j':
        case ARROW_DOWN:
            YEXE("cursor-down");
            break;

        case 'k':
        case ARROW_UP:
            YEXE("cursor-up");
            break;

        case 'l':
        case ARROW_RIGHT:
            YEXE("cursor-right");
            break;

        case PAGE_UP:
            YEXE("cursor-page-up");
            break;

        case PAGE_DOWN:
            YEXE("cursor-page-down");
            break;

        case 'w':
            YEXE("cursor-next-word");
            break;

        case 'b':
            YEXE("cursor-prev-word");
            break;

        case 'e':
            YEXE("cursor-next-word-end");
            break;

        case 'O':
            break;

        case 'o':
            break;

        case '0':
        case HOME_KEY:
            YEXE("cursor-line-begin");
            break;

        case '$':
        case END_KEY:
            YEXE("cursor-line-end");
            break;

        case '{':
            YEXE("cursor-prev-paragraph");
            break;

        case '}':
            YEXE("cursor-next-paragraph");
            break;

        case 'g':
            YEXE("cursor-buffer-begin");
            break;

        case 'G':
            YEXE("cursor-buffer-end");
            break;

        case '/':
            YEXE("find-in-buffer");
            break;

        case '?':
            YEXE("replace-current-search");
            break;

        case 'n':
            YEXE("find-next-in-buffer");
            break;

        case 'N':
            YEXE("find-prev-in-buffer");
            break;

        case 'f':
        case 't':
            till_pending = 1;
            last_till_op = key;
            break;

        case 'F':
        case 'T':
            till_pending = 2 + (key == 'T');
            last_till_op = key;
            break;

        case ';':
            vim_repeat_till();
            break;

        default:
            return 0;
    }

out:
    return 1;
}

void vim_normal(int key, char *key_str) {
    if (vim_nav_common(key, key_str)) {
        return;
    }

    switch (key) {
        case 'd':
            YEXE("select-off");
            vim_start_repeat(key);
            vim_change_mode(MODE_DELETE, 0, 0);
            break;

        case 'D':
            YEXE("select-off");
            vim_start_repeat(key);
            vim_change_mode(MODE_DELETE, 1, 0);
            break;

        case 'y':
            YEXE("select-off");
            vim_change_mode(MODE_YANK, 0, 0);
            break;

        case 'Y':
            YEXE("select-off");
            vim_change_mode(MODE_YANK, 1, 0);
            break;

        case 'v':
            YEXE("select");
            break;

        case 'V':
            YEXE("select-lines");
            break;

        case 'p':
            vim_start_repeat(key);
            YEXE("paste-yank-buffer");
            break;

        case 'a':
            YEXE("select-off");
            vim_start_repeat(key);
            YEXE("cursor-right");
            goto enter_insert;

        case 'A':
            YEXE("select-off");
            vim_start_repeat(key);
            YEXE("cursor-line-end");
            goto enter_insert;

        case 'i':
            YEXE("select-off");
            vim_start_repeat(key);
enter_insert:
            vim_change_mode(MODE_INSERT, 0, 0);
            break;

        case DEL_KEY:
            YEXE("select-off");
            vim_start_repeat(key);
            YEXE("delete-forward");
            break;

        case 'u':
            YEXE("undo");
            break;

        case CTRL_R:
            YEXE("redo");
            break;

        case '.':
            YEXE("select-off");
            vim_repeat();
            break;

        case ':':
            YEXE("command-prompt");
            break;

        case ESC:
        case CTRL_C:
            YEXE("select-off");
            break;

        case CTRL_Z:
            YEXE("suspend");
            break;

        default:
            yed_cerr("[NORMAL] unhandled key %d", key);
    }
}

void vim_insert(int key, char *key_str) {
    vim_push_repeat_key(key);

    switch (key) {
        case ARROW_LEFT:
            YEXE("cursor-left");
            break;

        case ARROW_DOWN:
            YEXE("cursor-down");
            break;

        case ARROW_UP:
            YEXE("cursor-up");
            break;

        case ARROW_RIGHT:
            YEXE("cursor-right");
            break;

        case PAGE_UP:
            YEXE("cursor-page-up");
            break;

        case PAGE_DOWN:
            YEXE("cursor-page-down");
            break;

        case HOME_KEY:
            YEXE("cursor-line-begin");
            break;

        case END_KEY:
            YEXE("cursor-line-end");
            break;

        case BACKSPACE:
            YEXE("delete-back");
            break;

        case DEL_KEY:
            YEXE("delete-forward");
            break;

        case ESC:
        case CTRL_C:
            vim_change_mode(MODE_NORMAL, 0, 1);
            break;

        default:
            if (key == ENTER || key == TAB || key == MBYTE || !iscntrl(key)) {
                YEXE("insert", key_str);
            } else {
                vim_pop_repeat_key();
                yed_cerr("[INSERT] unhandled key %d", key);
            }
    }

}

void vim_delete(int key, char *key_str) {
    vim_push_repeat_key(key);

    if (vim_nav_common(key, key_str)) {
        return;
    }

    switch (key) {
        case 'd':
            vim_change_mode(MODE_NORMAL, 0, 0);
            break;

        case 'c':
            vim_change_mode(MODE_NORMAL, 0, 0);
            vim_change_mode(MODE_INSERT, 0, 0);
            break;

        case ESC:
        case CTRL_C:
            vim_change_mode(MODE_NORMAL, 0, 1);
            break;

        default:
            vim_pop_repeat_key();
            yed_cerr("[DELETE] unhandled key %d", key);
    }
}

void vim_yank(int key, char *key_str) {
    if (vim_nav_common(key, key_str)) {
        return;
    }

    switch (key) {
        case 'y':
            vim_change_mode(MODE_NORMAL, 0, 0);
            break;

        case ESC:
        case CTRL_C:
            vim_change_mode(MODE_NORMAL, 0, 1);
            break;

        default:
            yed_cerr("[YANK] unhandled key %d", key);
    }
}

void vim_write(int n_args, char **args) {
    yed_execute_command("write-buffer", n_args, args);
}

void vim_quit(int n_args, char **args) {
    yed_frame *frame;

    if (array_len(ys->frames) > 1) {
        YEXE("frame-delete");
    } else {
        if (array_len(ys->frames) == 1) {
            frame = *(yed_frame**)array_item(ys->frames, 0);
            if (frame->top == 1 && frame->left == 1 &&
                frame->height == ys->term_rows - 2 && frame->width == ys->term_cols) {
                YEXE("quit");
            } else {
                YEXE("frame-delete");
            }
        } else {
            YEXE("quit");
        }
    }
}

void vim_write_quit(int n_args, char **args) {
    YEXE("w");
    YEXE("q");
}

void enter_insert(void) {
    yed_frame  *frame;
    yed_buffer *buff;

    frame = ys->active_frame;

    if (frame) {
        buff = frame->buffer;

        if (buff) {
            num_undo_records_before_insert = yed_get_undo_num_records(buff);
        }
    }

    if (yed_get_var("vim-insert-no-cursor-line") && yed_get_var("cursor-line")) {
        restore_cursor_line = 1;
        yed_set_var("cursor-line", "no");
    }
}

void exit_insert(void) {
    yed_frame  *frame;
    yed_buffer *buff;

    frame = ys->active_frame;

    if (frame) {
        buff = frame->buffer;

        if (buff) {

            while (yed_get_undo_num_records(buff) > num_undo_records_before_insert + 1) {
                yed_merge_undo_records(buff);
            }
        }
    }

    if (restore_cursor_line && yed_get_var("vim-insert-no-cursor-line")) {
        yed_set_var("cursor-line", "yes");
        restore_cursor_line = 0;
    }
}

void enter_delete(int by_line) {
    yed_set_var("enable-search-cursor-move", "yes");
    if (by_line) {
        YEXE("select-lines");
    } else {
        YEXE("select");
    }

}

void enter_yank(int by_line) {
    yed_set_var("enable-search-cursor-move", "yes");
    if (by_line) {
        YEXE("select-lines");
    } else {
        YEXE("select");
    }
}

void exit_delete(int cancel) {
    yed_frame  *frame;
    yed_buffer *buff;
    yed_range  *sel;
    char       *preserve;

    if (!cancel) {
        if (ys->active_frame
        &&  ys->active_frame->buffer
        &&  ys->active_frame->buffer->has_selection) {

            frame = ys->active_frame;
            buff  = frame->buffer;
            sel   = &buff->selection;

            if (sel->kind != RANGE_LINE
            &&  sel->anchor_row == sel->cursor_row
            &&  sel->anchor_col == sel->cursor_col) {

                YEXE("select-lines");
            }

            preserve = "1";
            YEXE("yank-selection", preserve);
            YEXE("delete-back");
        }
    }

    YEXE("select-off");
}

void exit_yank(int cancel) {
    yed_frame  *frame;
    yed_buffer *buff;
    yed_range  *sel;

    if (!cancel) {
        if (ys->active_frame
        &&  ys->active_frame->buffer
        &&  ys->active_frame->buffer->has_selection) {

            frame = ys->active_frame;
            buff  = frame->buffer;
            sel   = &buff->selection;

            if (sel->kind != RANGE_LINE
            &&  sel->anchor_row == sel->cursor_row
            &&  sel->anchor_col == sel->cursor_col) {

                YEXE("select-lines");
            }

            YEXE("yank-selection");
        }
    }

    YEXE("select-off");
}
