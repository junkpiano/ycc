#include <ctype.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "ycc.h"

//
// Tokenizer
//

// Token currently being parsed, and the whole input it points into.
Token *token;
char *user_input;

bool consume(char *op) {
    if (token->kind != TK_RESERVED || 
    (int)strlen(op) != token->len ||
    /* if they match each other, it's 0(false). Anything that is not 0 is true in C. */
    memcmp(token->str, op, token->len)) {
        return false;
    }

    token = token->next;
    return true;
}

void expect(char *op) {
    if (token->kind != TK_RESERVED ||
    (int)strlen(op) != token->len ||
    memcmp(token->str, op, token->len)) {
        error_at(token->str, "This is not '%s'", op);
    }

    token = token->next;
}

int expect_number(void) {
    if(token->kind != TK_NUM) {
        error_at(token->str, "This is not a number.");
    }

    int val = token->val;
    token = token->next;
    return val;
}

// Consume and return the current token if it is an identifier, else NULL.
Token *consume_ident(void) {
    if (token->kind != TK_IDENT) {
        return NULL;
    }

    Token *tok = token;
    token = token->next;
    return tok;
}

// Consume the current token if it is the given keyword kind.
bool consume_kind(TokenKind kind) {
    if (token->kind != kind) {
        return false;
    }

    token = token->next;
    return true;
}

bool at_eof(void) {
    return token->kind == TK_EOF;
}

static Token *new_token(TokenKind kind, Token *cur, char *str, int len) {
    // initialize values as zero. compare with malloc.
    Token *tok = calloc(1, sizeof(Token));
    tok->kind = kind;
    tok->str = str;
    tok->len = len;
    cur->next = tok;
    return tok;
}

static const struct {
    char *name;
    TokenKind kind;
} keywords[] = {
    {"return", TK_RETURN},
    {"if", TK_IF},
    {"else", TK_ELSE},
    {"while", TK_WHILE},
    {"for", TK_FOR},
    {"int", TK_INT},
    {"sizeof", TK_SIZEOF},
};

// The token kind for an identifier of the given length: a keyword kind if it
// matches one exactly, otherwise TK_IDENT.
static TokenKind keyword_kind(char *str, int len) {
    for (size_t i = 0; i < sizeof(keywords) / sizeof(*keywords); i++) {
        if ((int)strlen(keywords[i].name) == len &&
            memcmp(str, keywords[i].name, len) == 0) {
            return keywords[i].kind;
        }
    }

    return TK_IDENT;
}

static bool is_ident_head(char c) {
    return ('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z') || c == '_';
}

static bool is_ident_tail(char c) {
    return is_ident_head(c) || ('0' <= c && c <= '9');
}

static bool startswith(char *p, char *q) {
    return memcmp(p, q, strlen(q)) == 0;
}

static Token *tokenize(char *p) {
    Token head;
    head.next = NULL;
    Token *cur = &head;

    while(*p) {
        if (isspace(*p)) {
            p++;
            continue;
        }

        if (startswith(p, "==") ||
        startswith(p, "!=") ||
        startswith(p, "<=") ||
        startswith(p, ">=")) {
            cur = new_token(TK_RESERVED, cur, p, 2);
            p += 2;
            continue;
        }

        // An identifier: [A-Za-z_][A-Za-z0-9_]*
        if (is_ident_head(*p)) {
            char *q = p;
            do {
                p++;
            } while (is_ident_tail(*p));

            // A keyword is only a keyword when it is the whole identifier,
            // so "returnx" and "iffy" stay single identifiers.
            cur = new_token(keyword_kind(q, p - q), cur, q, p - q);
            continue;
        }

        // If char(*p) matches any of "+-*/()" or not
        if (strchr("+-*/()<>;={},&", *p)) {
            cur = new_token(TK_RESERVED, cur, p++, 1);
            continue;
        }

        // If char(*p) is number or not
        if (isdigit(*p)) {
            cur = new_token(TK_NUM, cur, p, 0);
            char *q = p;
            // convert p(startptr) into number until an invalid num char is found.
            // &p is address at the invalid num char(endptr)
            cur->val = strtol(p, &p, 10);
            cur->len = p - q;
            continue;
        }

        error_at(p, "invalid token");
    }

    new_token(TK_EOF, cur, p, 0);
    return head.next;
}

void init_token(char *p) {
    // Must be set before tokenize(), which may call error_at().
    user_input = p;
    token = tokenize(p);
}
