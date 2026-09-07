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
    TK_INT,
    TK_SIZEOF,
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
// Types
//

typedef enum {
    TY_INT,
    TY_PTR,
} TypeKind;

typedef struct Type Type;

struct Type {
    TypeKind kind;
    Type *ptr_to; // TY_PTR only
};

Type *int_type(void);
Type *pointer_to(Type *base);
int type_size(Type *ty);
bool is_pointer(Type *ty);

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
    Type *ty;
};

// Locals of the function being parsed, most recently declared first.
extern LVar *locals;



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
    ND_FUNCALL,
    ND_ADDR,
    ND_DEREF,
    ND_SIZEOF,
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
    // ND_FUNCALL: the arguments, chained through next.
    Node *body;
    Node *next;

    Type *ty; // set by add_type()

    // ND_FUNCALL
    char *funcname;
    int funcname_len;
    Node *args;
    int nargs;

    int val;    // ND_NUM only
    int offset; // ND_LVAR only: distance below rbp
};

// program = function+
// function = declspec ident "(" (declspec ident ("," declspec ident)*)? ")"
//            "{" stmt* "}"
// declspec = "int" "*"*
// stmt = "{" stmt* "}"
//      | declspec ident ";"
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
// unary = "sizeof" unary
//       | ("+" | "-" | "*" | "&") unary
//       | primary
// primary = num
//         | ident ("(" (expr ("," expr)*)? ")")?
//         | "(" expr ")"

// A parsed function definition.
typedef struct Function Function;

struct Function {
    Function *next;
    char *name;
    int name_len;
    Node *body;      // an ND_BLOCK
    Type *ret_ty;
    LVar *params;    // the first entries of locals, in declared order
    int nparams;
    int frame_size;  // a multiple of 16
};

// Every function in the program, in source order.
extern Function *functions;

void program(void);
void add_type(Node *node);
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
// Argument registers, in order.
#define MAX_ARGS 6

void gen(Node *node);
void gen_program(void);
bool stack_misaligned(void);
int stack_depth(void);



#endif