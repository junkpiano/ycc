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
