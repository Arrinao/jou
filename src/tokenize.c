// Implementation of the tokenize() function.

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "jou_compiler.h"
#include "util.h"


struct State {
    FILE *f;
    Location location;
    List(char) pushback;
};

static char read_byte(struct State *st) {
    int c;
    if (st->pushback.len != 0) {
        c = Pop(&st->pushback);
        goto got_char;
    }

    c = fgetc(st->f);
    if (c == EOF && ferror(st->f))
        fail_with_error(st->location, "cannot read file: %s", strerror(errno));

    // For Windows: \r\n in source file is treated same as \n
    if (c == '\r') {
        c = fgetc(st->f);
        if (c == EOF && ferror(st->f))
            fail_with_error(st->location, "cannot read file: %s", strerror(errno));
        if (c != '\n')
            fail_with_error(st->location, "source file contains a CR byte ('\\r') that isn't a part of a CRLF line ending");
    }

    // Use the zero byte to represent end of file.
    if (c == '\0')
        fail_with_error(st->location, "source file contains a zero byte");  // TODO: test this
    if (c == EOF) {
        assert(!ferror(st->f));
        return '\0';
    }

got_char:
    if (c == '\n')
        st->location.lineno++;
    return c;
}

// Can't use ungetc() because it is only guaranteed to work for one byte.
static void unread_byte(struct State *st, char c)
{
    if (c == '\0')
        return;
    assert(c!='\r');  // c should be from read_byte()
    Append(&st->pushback, c);
    if (c == '\n')
        st->location.lineno--;
}

static bool is_identifier_or_number_byte(char c)
{
    return ('A'<=c && c<='Z') || ('a'<=c && c<='z') || c=='_' || ('0'<=c && c<='9');
}

static void read_identifier_or_number(struct State *st, char firstbyte, char (*dest)[100])
{
    memset(*dest, 0, sizeof *dest);
    int destlen = 0;

    assert(is_identifier_or_number_byte(firstbyte));
    (*dest)[destlen++] = firstbyte;

    while(1) {
        char c = read_byte(st);
        if (!is_identifier_or_number_byte(c)) {
            unread_byte(st, c);
            return;
        }

        if (destlen == sizeof *dest - 1)
            fail_with_error(st->location, "name is too long: %.20s...", *dest);
        (*dest)[destlen++] = c;
    }
}

static void consume_rest_of_line(struct State *st)
{
    while(1) {
        char c = read_byte(st);
        if (c == '\n') {
            unread_byte(st, '\n');
            return;
        }
        if (c == '\0')  // end of file
            return;
    }
}

// Assumes that the initial '\n' byte has been read already.
static void read_indentation_as_newline_token(struct State *st, Token *t)
{
    t->type = TOKEN_NEWLINE;

    while(1) {
        char c = read_byte(st);
        if (c == ' ')
            t->data.indentation_level++;
        else if (c == '\n')
            t->data.indentation_level = 0;
        else if (c == '#')
            consume_rest_of_line(st);
        else if (c == '\0') {
            // Ignore newline+spaces at end of file. Do not validate 4 spaces.
            // TODO: test case
            t->type = TOKEN_END_OF_FILE;
            return;
        } else {
            unread_byte(st, c);
            break;
        }
    }
}

static long long parse_integer(const char *str, Location location)
{
    int base;
    const char *digits, *valid_digits;
    if (str[0] == '0' && str[1] == 'x') {
        // 0x123 = hexadecimal number
        base = 16;
        digits = &str[2];
        valid_digits = "0123456789ABCDEFabcdef";
    } else if (str[0] == '0' && str[1] == 'b') {
        // 0b1101 = binary number
        base = 2;
        digits = &str[2];
        valid_digits = "01";
    } else if (str[0] == '0' && str[1] != '\0') {
        // 0777 in C actually means 511. Jou does not allow writing 0777.
        fail_with_error(location, "unnecessary zero at start of number");
    } else {
        base = 10;
        digits = &str[0];
        valid_digits = "0123456789";
    }

    if (!*digits || strspn(digits, valid_digits) != strlen(digits))
        fail_with_error(location, "invalid number or variable name \"%s\"", str);

    return strtoll(digits, NULL, base);
}

static bool is_keyword(const char *s)
{
    const char *keywords[] = {
        "def", "declare", "struct",
        "return", "if", "elif", "else", "while", "for", "break", "continue",
        "True", "False", "NULL",
        "and", "or", "not", "as",
        "void", "bool", "byte", "int",
    };
    for (const char **kw = &keywords[0]; kw < &keywords[sizeof(keywords)/sizeof(keywords[0])]; kw++)
        if (!strcmp(*kw, s))
            return true;
    return false;
}

// Assumes the initial quote has been read already
static char *read_string(struct State *st, char quote, int *len)
{
    assert(quote == '\'' || quote == '"');
    List(char) result = {0};

    char c, after_backslash;
    while( (c=read_byte(st)) != quote )
    {
        switch(c) {
        case '\n':
            st->location.lineno--;  // to get error at the correct line number
            goto missing_end_quote;
        case '\0':
            goto missing_end_quote;
        case '\\':
            // \n means newline, for example
            after_backslash = read_byte(st);
            switch(after_backslash) {
            case 'n':
                Append(&result, '\n');
                break;
            case 'r':
                Append(&result, '\r');
                break;
            case '\\':
            case '\'':
            case '"':
                Append(&result, after_backslash);
                break;
            case '0':
                if (quote == '"')
                    fail_with_error(st->location, "strings cannot contain zero bytes (\\0), because that is the special end marker byte");
                __attribute__((fallthrough));
            case '1':
            case '2':
            case '3':
            case '4':
            case '5':
            case '6':
            case '7':
            case '8':
            case '9':
                Append(&result, after_backslash - '0');
                break;
            case '\n':
                // \ at end of line, string continues on next line
                if (quote == '\'') {
                    // TODO: tests
                    st->location.lineno--;  // to get error at the correct line number
                    goto missing_end_quote;
                }
                break;
            case '\0':
                goto missing_end_quote;
            default:
                if ((unsigned char)after_backslash < 0x80 && isprint(after_backslash))
                    fail_with_error(st->location, "unknown escape: '\\%c'", after_backslash);
                else
                    fail_with_error(st->location, "unknown '\\' escape");
            }
            break;
        default:
            Append(&result, c);
            break;
        }
    }

    if(len)
        *len = result.len;
    Append(&result, '\0');
    return result.ptr;

missing_end_quote:
    // TODO: tests
    if (quote == '"')
        fail_with_error(st->location, "missing \" to end the string");
    else
        fail_with_error(st->location, "missing ' to end the character");
}

static char read_char_literal(struct State *st)
{
    int len;
    char *s = read_string(st, '\'', &len);
    if (len == 0)
        fail_with_error(st->location, "empty character literal: ''");
    if (len >= 2)
        fail_with_error(st->location, "single quotes are for a single character, maybe use double quotes to instead make a string?");
    char result = s[0];
    free(s);
    return result;
}

static const char *const operatorChars = "=<>!.,()[]{};:+-*/&";

static const char *read_operator(struct State *st)
{
    const char *operators[] = {
        // Longer operators first, so that '==' does not parse as '=' '='
        "...", "===", "!==",
        "==", "!=", "->", "<=", ">=", "++", "--",
        ".", ",", ":", ";", "=", "(", ")", "{", "}", "[", "]", "&", "*", "/", "+", "-", "<", ">",
        NULL,
    };

    char operator[4] = {0};
    while(strlen(operator) < 3) {
        char c = read_byte(st);
        if (c=='\0')
            break;
        if(!strchr(operatorChars, c)) {
            unread_byte(st, c);
            break;
        }
        operator[strlen(operator)] = c;
    }

    for (const char *const *op = operators; *op; op++) {
        if (!strncmp(operator, *op, strlen(*op))) {
            // Unread the bytes we didn't use.
            for (int i = strlen(operator) - 1; i >= (int)strlen(*op); i--)
                unread_byte(st, operator[i]);
            // "===" and "!==" are here only to give a better error message to javascript people.
            if (!strcmp(*op, "===") || !strcmp(*op, "!=="))
                break;
            return *op;
        }
    }

    fail_with_error(st->location, "there is no '%s' operator", operator);
}

static Token read_token(struct State *st)
{
    Token t = { .location = st->location };

    while(1) {
        char c = read_byte(st);
        switch(c) {
        case '#': consume_rest_of_line(st); continue;
        case ' ': continue;
        case '\n': read_indentation_as_newline_token(st, &t); break;
        case '\0': t.type = TOKEN_END_OF_FILE; break;
        case '\'': t.type = TOKEN_CHAR; t.data.char_value = read_char_literal(st); break;
        case '"': t.type = TOKEN_STRING; t.data.string_value = read_string(st, '"', NULL); break;
        default:
            if(is_identifier_or_number_byte(c)) {
                read_identifier_or_number(st, c, &t.data.name);
                if (is_keyword(t.data.name))
                    t.type = TOKEN_KEYWORD;
                else if ('0'<=t.data.name[0] && t.data.name[0]<='9') {
                    t.type = TOKEN_INT;
                    t.data.int_value = parse_integer(t.data.name, t.location);
                } else {
                    t.type = TOKEN_NAME;
                }
            } else if (strchr(operatorChars, c)) {
                unread_byte(st, c);
                t.type = TOKEN_OPERATOR;
                strcpy(t.data.operator, read_operator(st));
            } else {
                if ((unsigned char)c < 0x80 && isprint(c))
                    fail_with_error(st->location, "unexpected byte '%c' (%#02x)", c, (unsigned char)c);
                else
                    fail_with_error(st->location, "unexpected byte %#02x", (unsigned char)c);
            }
            break;
        }
        return t;
    }
}

static Token *tokenize_without_indent_dedent_tokens(const char *filename)
{
    struct State st = { .location.filename=filename, .f = fopen(filename, "rb") };
    if (!st.f)
        fail_with_error(st.location, "cannot open file: %s", strerror(errno));

    /*
    Add a fake newline to the beginning. It does a few things:
      * Less special-casing: blank lines in the beginning of the file can
        cause there to be a newline token anyway.
      * It is easier to detect an unexpected indentation in the beginning
        of the file, as it becomes just like any other indentation.
      * Line numbers start at 1.
    */
    Append(&st.pushback, '\n');

    List(Token) tokens = {0};
    while(tokens.len == 0 || tokens.ptr[tokens.len-1].type != TOKEN_END_OF_FILE)
        Append(&tokens, read_token(&st));

    free(st.pushback.ptr);
    fclose(st.f);
    return tokens.ptr;
}

// Add indent/dedent tokens after newline tokens that change the indentation level.
static Token *handle_indentations(const Token *temp_tokens)
{
    List(Token) tokens = {0};
    const Token *t = temp_tokens;
    int level = 0;

    do{
        if (t->type == TOKEN_END_OF_FILE) {
            // Add an extra newline token at end of file and the dedents after it.
            // This makes it similar to how other newline and dedent tokens work:
            // the dedents always come after a newline token.
            Append(&tokens, (Token){ .location=t->location, .type=TOKEN_NEWLINE });
            while(level) {
                Append(&tokens, (Token){ .location=t->location, .type=TOKEN_DEDENT });
                level -= 4;
            }
        }

        Append(&tokens, *t);

        if (t->type == TOKEN_NEWLINE) {
            Location after_newline = t->location;
            after_newline.lineno++;

            if (t->data.indentation_level % 4 != 0)
                fail_with_error(after_newline, "indentation must be a multiple of 4 spaces");

            while (level < t->data.indentation_level) {
                Append(&tokens, (Token){ .location=after_newline, .type=TOKEN_INDENT });
                level += 4;
            }

            while (level > t->data.indentation_level) {
                Append(&tokens, (Token){ .location=after_newline, .type=TOKEN_DEDENT });
                level -= 4;
            }
        }
    } while (t++->type != TOKEN_END_OF_FILE);

    /*
    Delete the newline token in the beginning.

    If the file has indentations after it, they are now represented by separate
    indent tokens and parsing will fail. If the file doesn't have any blank/comment
    lines in the beginning, it has a newline token anyway to avoid special casing.
    */
    assert(tokens.ptr[0].type == TOKEN_NEWLINE);
    memmove(&tokens.ptr[0], &tokens.ptr[1], sizeof(tokens.ptr[0]) * (tokens.len - 1));

    return tokens.ptr;
}

Token *tokenize(const char *filename)
{
    Token *tokens1 = tokenize_without_indent_dedent_tokens(filename);
    Token *tokens2 = handle_indentations(tokens1);
    free(tokens1);
    return tokens2;
}
