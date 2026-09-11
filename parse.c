/**
 * @file parse.c
 * @author Yusuke Ohashi(mail@yusuke.cloud)
 * @brief
 * @version 0.1
 * @date 2022-08-24
 *
 * @copyright Copyright (c) 2022 Yusuke Ohashi
 *
 */

#include <stdlib.h>
#include <string.h>

#include "ycc.h"

//
// Parser
//

static Node *new_node(NodeKind kind) {
    Node *node = calloc(1, sizeof(Node));
    node->kind = kind;
    return node;
}

static Node *new_binary(NodeKind kind, Node *lhs, Node *rhs) {
    Node *node = new_node(kind);
    node->lhs = lhs;
    node->rhs = rhs;
    return node;
}

static Node *new_num(int val) {
    Node *node = new_node(ND_NUM);
    node->val = val;
    return node;
}

Function *functions;
LVar *locals;
LVar *globals;

static LVar *find_in(LVar *list, Token *tok) {
    for (LVar *var = list; var != NULL; var = var->next) {
        if (var->len == tok->len && memcmp(var->name, tok->str, var->len) == 0) {
            return var;
        }
    }

    return NULL;
}

// Find a local by name, or NULL. Linear scan is fine at this scale.
static LVar *find_lvar(Token *tok) {
    return find_in(locals, tok);
}

// Locals shadow globals, so the local list is searched first.
static LVar *find_var(Token *tok) {
    LVar *var = find_in(locals, tok);
    return var != NULL ? var : find_in(globals, tok);
}

static LVar *new_gvar(Token *tok, Type *ty) {
    LVar *var = calloc(1, sizeof(LVar));
    var->next = globals;
    var->name = tok->str;
    var->len = tok->len;
    var->ty = ty;
    var->is_global = true;
    globals = var;
    return var;
}

// Append a local, giving it the next slot below the last one.
static LVar *new_lvar(Token *tok, Type *ty) {
    LVar *var = calloc(1, sizeof(LVar));
    var->next = locals;
    var->name = tok->str;
    var->len = tok->len;
    var->ty = ty;
    var->offset = (locals == NULL ? 0 : locals->offset) + type_size(ty);
    locals = var;
    return var;
}

// ident ("[" num "]")*
//
// The suffixes read left to right but nest outside in, so "int a[2][3]" is an
// array of 2 arrays of 3 ints. Collect the lengths, then apply them backwards.
static Type *declarator(Type *ty, Token **name) {
    *name = consume_ident();
    if (*name == NULL) {
        error_at(token->str, "expected a variable name");
    }

    int lens[8];
    int ndims = 0;
    while (consume("[")) {
        if (ndims == 8) {
            error_at(token->str, "too many array dimensions");
        }
        int len = expect_number();
        if (len <= 0) {
            error_at(token->str, "array length must be positive");
        }
        lens[ndims++] = len;
        expect("]");
    }

    for (int i = ndims - 1; i >= 0; i--) {
        ty = array_of(ty, lens[i]);
    }

    return ty;
}

// "int" "*"*
static Type *declspec(void) {
    if (!consume_kind(TK_INT)) {
        return NULL;
    }

    Type *ty = int_type();
    while (consume("*")) {
        ty = pointer_to(ty);
    }

    return ty;
}

// Only known once the whole function has been parsed.
// Rounded up to 16 so that rsp is 16-byte aligned once the frame is reserved.
// The System V ABI requires that at a call, and keeping the base aligned means
// alignment then depends only on how many values are pushed.
static int frame_size(void) {
    int size = locals == NULL ? 0 : locals->offset;
    return (size + 15) / 16 * 16;
}

// function = ident "(" (ident ("," ident)*)? ")" "{" stmt* "}"
static Function *function(void) {
    Type *ret_ty = declspec();
    if (ret_ty == NULL) {
        error_at(token->str, "expected a type");
    }

    Token *name = consume_ident();
    if (name == NULL) {
        error_at(token->str, "expected a function name");
    }
    if (find_in(globals, name) != NULL) {
        error_at(name->str, "a global of this name is already declared");
    }

    // Each function has its own locals, so the list starts empty. Parameters
    // are simply the first ones declared.
    locals = NULL;

    Function *fn = calloc(1, sizeof(Function));
    fn->name = name->str;
    fn->name_len = name->len;
    fn->ret_ty = ret_ty;

    expect("(");
    if (!consume(")")) {
        for (;;) {
            Type *ty = declspec();
            if (ty == NULL) {
                error_at(token->str, "expected a parameter type");
            }
            Token *param = consume_ident();
            if (param == NULL) {
                error_at(token->str, "expected a parameter name");
            }
            if (find_lvar(param) != NULL) {
                error_at(param->str, "duplicate parameter");
            }
            new_lvar(param, ty);
            fn->nparams++;
            if (!consume(",")) {
                break;
            }
        }
        expect(")");
    }
    if (fn->nparams > MAX_ARGS) {
        error_at(name->str, "too many parameters (max %d)", MAX_ARGS);
    }
    // locals currently holds exactly the parameters, newest first.
    fn->params = locals;

    if (!consume("{")) {
        error_at(token->str, "expected a function body");
    }
    Node head = {0};
    Node *cur = &head;
    while (!consume("}")) {
        if (at_eof()) {
            error_at(token->str, "unclosed function body");
        }
        cur->next = stmt();
        cur = cur->next;
    }
    fn->body = new_node(ND_BLOCK);
    fn->body->body = head.next;

    // Only known now that the whole body has been parsed.
    fn->frame_size = frame_size();
    return fn;
}

// Both start "int name", so telling them apart needs a look past the
// declarator to the "(" or the ";".
static bool is_function_definition(void) {
    Token *saved = token;

    // Scan the tokens rather than calling declspec(), which would allocate a
    // pointer type that the rewind then throws away.
    bool result = false;
    if (consume_kind(TK_INT)) {
        while (consume("*")) {
        }
        if (consume_ident() != NULL) {
            result = consume("(");
        }
    }

    token = saved;
    return result;
}

// A function and a global share one namespace: both become assembler symbols,
// and a collision would only surface as an assembler error.
static bool is_defined_function(Token *tok) {
    for (Function *fn = functions; fn != NULL; fn = fn->next) {
        if (fn->name_len == tok->len && memcmp(fn->name, tok->str, tok->len) == 0) {
            return true;
        }
    }

    return false;
}

static void global_declaration(void) {
    Type *ty = declspec();
    Token *name;
    ty = declarator(ty, &name);

    if (find_in(globals, name) != NULL) {
        error_at(name->str, "redeclared global");
    }
    if (is_defined_function(name)) {
        error_at(name->str, "a function of this name is already defined");
    }
    if (consume("=")) {
        error_at(token->str, "a global cannot have an initialiser yet");
    }

    new_gvar(name, ty);
    expect(";");
}

void program(void) {
    Function head = {0};
    Function *cur = &head;

    while (!at_eof()) {
        if (is_function_definition()) {
            cur->next = function();
            cur = cur->next;
            // Published as we go, so a later declaration can see it.
            functions = head.next;
        } else {
            global_declaration();
        }
    }

    if (head.next == NULL) {
        error("no function defined");
    }

    functions = head.next;
}

Node *stmt(void) {
    Node *node;

    // The null statement, as in "while (...) ;".
    if (consume(";")) {
        return new_node(ND_NOP);
    }

    // A declaration: "int x;" or "int *p;". It reserves a slot and produces
    // no code, but is still a statement, so it leaves a value like any other.
    Type *ty = declspec();
    if (ty != NULL) {
        Token *name;
        ty = declarator(ty, &name);
        if (find_lvar(name) != NULL) {
            error_at(name->str, "redeclared variable");
        }
        new_lvar(name, ty);
        expect(";");
        return new_node(ND_NOP);
    }

    if (consume("{")) {
        Node head = {0};
        Node *cur = &head;

        while (!consume("}")) {
            if (at_eof()) {
                error_at(token->str, "unclosed block");
            }
            cur->next = stmt();
            cur = cur->next;
        }

        node = new_node(ND_BLOCK);
        node->body = head.next;
        return node;
    }

    if (consume_kind(TK_IF)) {
        node = new_node(ND_IF);
        expect("(");
        node->cond = expr();
        expect(")");
        node->then = stmt();
        // An else binds to the nearest if, which falls out of recursing here.
        if (consume_kind(TK_ELSE)) {
            node->els = stmt();
        }
        return node;
    }

    if (consume_kind(TK_WHILE)) {
        node = new_node(ND_WHILE);
        expect("(");
        node->cond = expr();
        expect(")");
        node->then = stmt();
        return node;
    }

    if (consume_kind(TK_FOR)) {
        node = new_node(ND_FOR);
        expect("(");
        // All three clauses are optional. An omitted condition is true, not
        // false, so for(;;) loops forever.
        if (!consume(";")) {
            node->init = expr();
            expect(";");
        }
        if (!consume(";")) {
            node->cond = expr();
            expect(";");
        }
        if (!consume(")")) {
            node->inc = expr();
            expect(")");
        }
        node->then = stmt();
        return node;
    }

    if (consume_kind(TK_RETURN)) {
        node = new_node(ND_RETURN);
        node->lhs = expr();
    } else {
        node = expr();
    }

    expect(";");
    return node;
}

Node *expr(void) {
    return assign();
}

Node *assign(void) {
    Node *node = equality();

    // Right associative, so recurse into assign() rather than looping.
    if (consume("=")) {
        node = new_binary(ND_ASSIGN, node, assign());
    }

    return node;
}

Node *equality(void) {
    Node *node = relational();
    
    for(;;) {
        if (consume("==")) {
            node = new_binary(ND_EQ, node, relational());
        } else if (consume("!=")) {
            node = new_binary(ND_NE, node, relational());
        } else {
            return node;
        }
    }
}

Node *relational(void) {
    Node *node = add();

    for(;;) {
        if (consume("<")) {
            node = new_binary(ND_LT, node, add());
        } else if (consume("<=")) {
            node = new_binary(ND_LE, node, add());
        } else if (consume(">")) {
            node = new_binary(ND_LT, add(), node);
        } else if (consume(">=")) {
            node = new_binary(ND_LE, add(), node);
        } else {
            return node;
        }
    }
}

Node *add(void) {
    Node *node = mul();

    for(;;) {
        if (consume("+")) {
            node = new_binary(ND_ADD, node, mul());
        } else if (consume("-")){
            node = new_binary(ND_SUB, node, mul());
        } else {
            return node;
        }
    }
}

Node *mul(void) {
    Node *node = unary();

    for(;;) {
        if (consume("*")) {
            node = new_binary(ND_MUL, node, unary());
        } else if (consume("/")) {
            node = new_binary(ND_DIV, node, unary());
        } else {
            return node;
        }
    }
}

Node *unary(void) {
    // Folded to a constant by add_type(), once the operand's type is known.
    if (consume_kind(TK_SIZEOF)) {
        Node *node = new_node(ND_SIZEOF);
        node->lhs = unary();
        return node;
    }

    if (consume("+")) {
        return unary();
    }

    if (consume("-")) {
        return new_binary(ND_SUB, new_num(0), unary());
    }

    if (consume("*")) {
        Node *node = new_node(ND_DEREF);
        node->lhs = unary();
        return node;
    }

    if (consume("&")) {
        Node *node = new_node(ND_ADDR);
        node->lhs = unary();
        return node;
    }

    return postfix();
}

Node *primary(void) {
    if (consume("(")) {
        Node *node = expr();
        expect(")");
        return node;
    }

    Token *tok = consume_ident();
    if (tok != NULL) {
        // A "(" here makes it a call rather than a variable reference.
        if (consume("(")) {
            Node *node = new_node(ND_FUNCALL);
            node->funcname = tok->str;
            node->funcname_len = tok->len;

            Node head = {0};
            Node *cur = &head;
            if (!consume(")")) {
                for (;;) {
                    cur->next = expr();
                    cur = cur->next;
                    node->nargs++;
                    if (!consume(",")) {
                        break;
                    }
                }
                expect(")");
            }
            if (node->nargs > MAX_ARGS) {
                error_at(tok->str, "too many arguments (max %d)", MAX_ARGS);
            }
            node->args = head.next;
            return node;
        }

        LVar *var = find_var(tok);
        if (var == NULL) {
            error_at(tok->str, "undeclared variable");
        }
        Node *node = new_node(ND_LVAR);
        node->var = var;
        node->ty = var->ty;
        return node;
    }

    return new_num(expect_number());
}

// Subscripting is defined as *(a + i), so the scaling in add_type() does the
// work and i[a] falls out for free.
Node *postfix(void) {
    Node *node = primary();

    while (consume("[")) {
        Node *index = expr();
        expect("]");

        Node *sum = new_binary(ND_ADD, node, index);
        node = new_node(ND_DEREF);
        node->lhs = sum;
    }

    return node;
}
