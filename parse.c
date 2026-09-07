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

Node *code[MAX_STATEMENTS + 1];

void program(void) {
    int i = 0;

    while (!at_eof()) {
        if (i == MAX_STATEMENTS) {
            error_at(token->str, "too many statements (max %d)", MAX_STATEMENTS);
        }
        code[i++] = stmt();
    }

    if (i == 0) {
        error("empty program");
    }

    code[i] = NULL;
}

Node *stmt(void) {
    Node *node = expr();
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
        Node *node = new_node(ND_LVAR);
        // 'a' lives closest to rbp, 'z' furthest.
        node->offset = (tok->str[0] - 'a' + 1) * 8;
        return node;
    }

    return new_num(expect_number());
}
