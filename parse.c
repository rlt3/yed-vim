#include <stdbool.h>

bool
is_movement (char c)
{
    return c == 'h' || c == 'j' || c == 'k' || c == 'l' ||
           c == 'w' || c == 'W' ||
           c == 'b' || c == 'B' ||
           c == 'e' || c == 'E';
}

bool
movement (int repeat, char c)
{
    char *cmd;

    switch (c) {
        case 'h': cmd = "cursor-left";  break;
        case 'j': cmd = "cursor-down";  break;
        case 'k': cmd = "cursor-up";    break;
        case 'l': cmd = "cursor-right"; break;

        case 'w': cmd = "cursor-next-word"; break;
        case 'W': cmd = "cursor-next-word"; break; /* TODO */

        case 'b': cmd = "cursor-prev-word"; break;
        case 'B': cmd = "cursor-prev-word"; break; /* TODO */

        case 'e': cmd = "cursor-next-word-end"; break;
        case 'E': cmd = "cursor-next-word-end"; break; /* TODO */
    }

    for (int i = 0; i < repeat; i++)
        YEXE(cmd);
}

void
expression (Tokens *T)
{
    array_t buff;
    int repeat;
    int c;

    repeat = 1;
    buff = array_make(char);

    tokens_reset(T);

    if (tokens_first(T) == ':') {
        YEXE("vim-command");
        return;
    }

    while (true) {
        c = tokens_next(T);

        if (c == EOF)
            break;

        if (is_digit(c)) {
            array_push(buff, c);
            continue;
        } else if (array_len(buff) > 0) {
            array_push(buff, EOS);
            sscanf(array_data(buff), "%d", &repeat);
        }

        if (is_movement(c)) {
            movement(repeat, c);
            tokens_clear(T);
            break;
        }

        yed_cerr("Unhandled key %d", c);
    }

    array_free(buff);
}
