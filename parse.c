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

Node *program_body;
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
static LVar *new_lvar(Token *tok) {
    LVar *var = calloc(1, sizeof(LVar));
    var->next = locals;
    var->name = tok->str;
    var->len = tok->len;
    var->offset = (locals == NULL ? 0 : locals->offset) + 8;
    locals = var;
    return var;
}

// Only known once the whole function has been parsed.
// Rounded up to 16 so that rsp is 16-byte aligned once the frame is reserved.
// The System V ABI requires that at a call, and keeping the base aligned means
// alignment then depends only on how many values are pushed.
int frame_size(void) {
    int size = locals == NULL ? 0 : locals->offset;
    return (size + 15) / 16 * 16;
}

// The top level is an implicit block, so there is no fixed statement limit.
void program(void) {
    Node head = {0};
    Node *cur = &head;

    while (!at_eof()) {
        cur->next = stmt();
        cur = cur->next;
    }

    if (head.next == NULL) {
        error("empty program");
    }

    program_body = new_node(ND_BLOCK);
    program_body->body = head.next;
}

Node *stmt(void) {
    Node *node;

    // The null statement, as in "while (...) ;".
    if (consume(";")) {
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
    if (consume("+")) {
        return unary();
    }

    if (consume("-")) {
        return new_binary(ND_SUB, new_num(0), unary());
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

        Node *node = new_node(ND_LVAR);
        LVar *var = find_lvar(tok);
        if (var == NULL) {
            var = new_lvar(tok);
        }
        node->offset = var->offset;
        return node;
    }

    return new_num(expect_number());
}
