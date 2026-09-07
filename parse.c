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

// Find a local by name, or NULL. Linear scan is fine at this scale.
static LVar *find_lvar(Token *tok) {
    for (LVar *var = locals; var != NULL; var = var->next) {
        if (var->len == tok->len && memcmp(var->name, tok->str, var->len) == 0) {
            return var;
        }
    }

    return NULL;
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

void program(void) {
    Function head = {0};
    Function *cur = &head;

    while (!at_eof()) {
        cur->next = function();
        cur = cur->next;
    }

    if (head.next == NULL) {
        error("empty program");
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
        Token *name = consume_ident();
        if (name == NULL) {
            error_at(token->str, "expected a variable name");
        }
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

    return primary();
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

        LVar *var = find_lvar(tok);
        if (var == NULL) {
            error_at(tok->str, "undeclared variable");
        }
        Node *node = new_node(ND_LVAR);
        node->offset = var->offset;
        node->ty = var->ty;
        return node;
    }

    return new_num(expect_number());
}
