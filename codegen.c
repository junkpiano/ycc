#include "ycc.h"

//
// Code Generator
//

// Evaluate both children, leaving the left operand in rax and the right in rdi.
static void gen_operands(Node *node) {
    gen(node->lhs);
    gen(node->rhs);

    printf("    pop rdi\n");
    printf("    pop rax\n");
}

// Push the address of a node that can be assigned to.
static void gen_lval(Node *node) {
    if (node->kind != ND_LVAR) {
        error("codegen: not an lvalue");
    }

    printf("    mov rax, rbp\n");
    printf("    sub rax, %d\n", node->offset);
    printf("    push rax\n");
}

// Each kind drives its own recursion, so an unknown kind is rejected before
// any child is dereferenced.
void gen(Node *node) {
    if (node == NULL) {
        error("codegen: null node");
    }

    switch (node->kind) {
        case ND_NUM:
        printf("    push %d\n", node->val);
        return;
        case ND_LVAR:
        gen_lval(node);
        printf("    pop rax\n");
        printf("    mov rax, [rax]\n");
        printf("    push rax\n");
        return;
        case ND_ASSIGN:
        gen_lval(node->lhs);
        gen(node->rhs);
        printf("    pop rdi\n");
        printf("    pop rax\n");
        printf("    mov [rax], rdi\n");
        // Assignment is an expression: its value is the value stored.
        printf("    push rdi\n");
        return;
        case ND_ADD:
        gen_operands(node);
        printf("    add rax, rdi\n");
        break;
        case ND_SUB:
        gen_operands(node);
        printf("    sub rax, rdi\n");
        break;
        case ND_MUL:
        gen_operands(node);
        printf("    imul rax, rdi\n");
        break;
        case ND_DIV:
        gen_operands(node);
        printf("    cqo\n");
        printf("    idiv rdi\n");
        break;
        case ND_EQ:
        gen_operands(node);
        printf("    cmp rax, rdi\n");
        printf("    sete al\n");
        printf("    movzb rax, al\n");
        break;
        case ND_NE:
        gen_operands(node);
        printf("    cmp rax, rdi\n");
        printf("    setne al\n");
        printf("    movzb rax, al\n");
        break;
        case ND_LT:
        gen_operands(node);
        printf("    cmp rax, rdi\n");
        printf("    setl al\n");
        printf("    movzb rax, al\n");
        break;
        case ND_LE:
        gen_operands(node);
        printf("    cmp rax, rdi\n");
        printf("    setle al\n");
        printf("    movzb rax, al\n");
        break;
        default:
        error("codegen: unhandled node kind %d", node->kind);
    }

    printf("    push rax\n");
}
