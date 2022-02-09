typedef struct tokens {
    array_t input;
    int idx;
} Tokens;

static Tokens _tokens;

void tokens_make  (Tokens *T);
void tokens_push  (Tokens *T, char c);
void tokens_reset (Tokens *T);
int  tokens_first (Tokens *T);
int  tokens_next  (Tokens *T);

void
tokens_make (Tokens *T)
{
    T->idx = 0;
    T->input = array_make_with_cap(char, 16);
}

void
tokens_clear (Tokens *T)
{
    T->idx = 0;
    array_clear(T->input);
}

void
tokens_push (Tokens *T, char c)
{
    array_grow_if_needed(T->input);
    array_push(T->input, c);
}

void
tokens_reset (Tokens *T)
{
    T->idx = 0;
}

int
tokens_first (Tokens *T)
{
    if (array_len(T->input) == 0)
        return EOF;
    return *(char*)array_item(T->input, 0);
}

int
tokens_next (Tokens *T)
{
    if (T->idx >= array_len(T->input))
        return EOF;
    char *c = array_item(T->input, T->idx);
    T->idx++;
    return (int) *c;
}
