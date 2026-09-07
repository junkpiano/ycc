#ifndef __YCC_H__
#define __YCC_H__

#include <stdbool.h>
#include <stdio.h>

//
// Tokenizer
//

typedef enum {
    TK_RESERVED,
    TK_IDENT,
    TK_RETURN,
    TK_IF,
    TK_ELSE,
    TK_WHILE,
    TK_FOR,
    TK_NUM,
    TK_EOF,
} TokenKind;

typedef struct Token Token;

struct Token {
    TokenKind kind;
    Token *next;
    int val;
    char *str;
    int len;
};

// Token currently being parsed, and the whole input it points into.
extern Token *token;
extern char *user_input;

void init_token(char *p);
bool consume(char *op);
void expect(char *op);
int expect_number(void);
Token *consume_ident(void);
bool consume_kind(TokenKind kind);
bool at_eof(void);

//
// Error reporting
//

void error(char *fmt, ...);
void error_at(char *loc, char *fmt, ...);

//
// Parser
//

// A local variable.
typedef struct LVar LVar;

struct LVar {
    LVar *next;
    char *name; // not NUL-terminated; points into the input
    int len;
    int offset; // distance below rbp
};

// Locals of the function being parsed, most recently declared first.
extern LVar *locals;

// Bytes of stack the current function's locals need.
int frame_size(void);

typedef enum {
    ND_ADD,
    ND_SUB,
    ND_MUL,
    ND_DIV,
    ND_EQ,  // ==
    ND_NE,  // !=
    ND_LT,  // <
    ND_LE,  // <=
    ND_ASSIGN,
    ND_LVAR,
    ND_RETURN,
    ND_IF,
    ND_WHILE,
    ND_FOR,
    ND_NOP,
    ND_BLOCK,
    ND_NUM,
} NodeKind;

typedef struct Node Node;

struct Node {
    NodeKind kind; // Node kind
    Node *lhs;
    Node *rhs;

    // ND_IF, ND_WHILE, ND_FOR
    Node *cond;
    Node *then;
    Node *els;
    Node *init;
    Node *inc;

    // ND_BLOCK: the statements, chained through next.
    Node *body;
    Node *next;

    int val;    // ND_NUM only
    int offset; // ND_LVAR only: distance below rbp
};

// program = stmt+
// stmt = "{" stmt* "}"
//      | ";"
//      | expr ";"
//      | "return" expr ";"
//      | "if" "(" expr ")" stmt ("else" stmt)?
//      | "while" "(" expr ")" stmt
//      | "for" "(" expr? ";" expr? ";" expr? ")" stmt
// expr = assign
// assign = equality ("=" assign)?
// equality = relational ("==" relational | "!=" relational)*
// relational = add ("<" add | "<=" add | ">" add | ">=" add)*
// add = mul ("+" mul | "-" mul)*
// mul = unary ("*" unary | "/" unary)*
// unary = ("+" | "-") unary | primary
// primary = num | ident | "(" expr ")"

// The whole program, as one implicit block.
extern Node *program_body;

void program(void);
Node *stmt(void);
Node *expr(void);
Node *assign(void);
Node *equality(void);
Node *relational(void);
Node *add(void);
Node *mul(void);
Node *unary(void);
Node *primary(void);

//
// Code Generator
//
void gen(Node *node);

// Label the epilogue jumps to.
#define RETURN_LABEL ".L.return"

#endif