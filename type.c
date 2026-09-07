#include <stdlib.h>
#include <string.h>

#include "ycc.h"

//
// Types
//

Type *int_type(void) {
    static Type ty = {TY_INT, NULL};
    return &ty;
}

Type *pointer_to(Type *base) {
    Type *ty = calloc(1, sizeof(Type));
    ty->kind = TY_PTR;
    ty->ptr_to = base;
    return ty;
}

int type_size(Type *ty) {
    // Everything occupies 8 bytes for now, int included. Narrower storage
    // arrives with char, which is where that assumption has to be revisited.
    (void)ty;
    return 8;
}

bool is_pointer(Type *ty) {
    return ty != NULL && ty->kind == TY_PTR;
}

// Look up a function defined in this program by the name a call uses.
static Function *find_function(Node *call) {
    for (Function *fn = functions; fn != NULL; fn = fn->next) {
        if (fn->name_len == call->funcname_len &&
            memcmp(fn->name, call->funcname, fn->name_len) == 0) {
            return fn;
        }
    }

    return NULL;
}


static Node *new_type_node(NodeKind kind) {
    Node *node = calloc(1, sizeof(Node));
    node->kind = kind;
    return node;
}

static Node *num_node(int val) {
    Node *node = new_type_node(ND_NUM);
    node->val = val;
    node->ty = int_type();
    return node;
}

// Multiply the integer side of pointer arithmetic by the pointee size, so that
// p + 1 advances one element rather than one byte.
static Node *scale_by(Node *node, int size) {
    Node *mul = new_type_node(ND_MUL);
    mul->lhs = node;
    mul->rhs = num_node(size);
    mul->ty = int_type();
    return mul;
}

static void scale_add(Node *node) {
    if (is_pointer(node->lhs->ty) && is_pointer(node->rhs->ty)) {
        error("cannot add two pointers");
    }

    // int + ptr is the same as ptr + int.
    if (!is_pointer(node->lhs->ty) && is_pointer(node->rhs->ty)) {
        Node *tmp = node->lhs;
        node->lhs = node->rhs;
        node->rhs = tmp;
    }

    if (is_pointer(node->lhs->ty)) {
        node->rhs = scale_by(node->rhs, type_size(node->lhs->ty->ptr_to));
    }

    node->ty = node->lhs->ty;
}

static void scale_sub(Node *node) {
    // ptr - ptr is a count of elements, not bytes.
    if (is_pointer(node->lhs->ty) && is_pointer(node->rhs->ty)) {
        Node *diff = new_type_node(ND_SUB);
        diff->lhs = node->lhs;
        diff->rhs = node->rhs;
        diff->ty = int_type();

        node->kind = ND_DIV;
        node->lhs = diff;
        node->rhs = num_node(type_size(diff->lhs->ty->ptr_to));
        node->ty = int_type();
        return;
    }

    if (!is_pointer(node->lhs->ty) && is_pointer(node->rhs->ty)) {
        error("cannot subtract a pointer from an integer");
    }

    if (is_pointer(node->lhs->ty)) {
        node->rhs = scale_by(node->rhs, type_size(node->lhs->ty->ptr_to));
    }

    node->ty = node->lhs->ty;
}

// Assign a type to every node, bottom up. Kept as a separate pass rather than
// typing during the parse: sizeof needs the type of an expression it never
// evaluates, and a call's type depends on a definition that may come later.
void add_type(Node *node) {
    if (node == NULL || node->ty != NULL) {
        return;
    }

    add_type(node->lhs);
    add_type(node->rhs);
    add_type(node->cond);
    add_type(node->then);
    add_type(node->els);
    add_type(node->init);
    add_type(node->inc);

    for (Node *n = node->body; n != NULL; n = n->next) {
        add_type(n);
    }
    for (Node *n = node->args; n != NULL; n = n->next) {
        add_type(n);
    }

    switch (node->kind) {
        case ND_ADD:
        scale_add(node);
        return;
        case ND_SUB:
        scale_sub(node);
        return;
        case ND_MUL:
        case ND_DIV:
        case ND_ASSIGN:
        node->ty = node->lhs->ty;
        return;
        case ND_EQ:
        case ND_NE:
        case ND_LT:
        case ND_LE:
        case ND_NUM:
        case ND_NOP:
        case ND_WHILE:
        case ND_FOR:
        case ND_IF:
        case ND_RETURN:
        node->ty = int_type();
        return;
        case ND_LVAR:
        // Set when the variable was resolved.
        return;
        case ND_ADDR:
        node->ty = pointer_to(node->lhs->ty);
        return;
        case ND_DEREF:
        if (!is_pointer(node->lhs->ty)) {
            error("cannot dereference a non-pointer");
        }
        node->ty = node->lhs->ty->ptr_to;
        return;
        case ND_FUNCALL: {
            Function *fn = find_function(node);
            // An undeclared callee is assumed to return int, which is what the
            // external helpers do.
            node->ty = fn != NULL && fn->ret_ty != NULL ? fn->ret_ty : int_type();
            return;
        }
        case ND_BLOCK:
        // The value of the last statement, or 0 when empty.
        node->ty = node->body == NULL ? int_type() : int_type();
        return;
    }
}
