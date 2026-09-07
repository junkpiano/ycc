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

// Distinguishes the labels of one control-flow construct from another.
static int label_seq = 0;

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
        case ND_IF: {
            // Every statement leaves exactly one value for the statement-level
            // pop, so both arms must push. An if with no else and a false
            // condition pushes 0.
            int seq = label_seq++;
            gen(node->cond);
            printf("    pop rax\n");
            printf("    cmp rax, 0\n");
            printf("    je .L.else.%d\n", seq);
            gen(node->then);
            printf("    jmp .L.end.%d\n", seq);
            printf(".L.else.%d:\n", seq);
            if (node->els != NULL) {
                gen(node->els);
            } else {
                printf("    push 0\n");
            }
            printf(".L.end.%d:\n", seq);
            return;
        }
        case ND_BLOCK:
        // A block's value is its last statement's, so every earlier value is
        // popped and the last one is left for whoever consumes this block.
        if (node->body == NULL) {
            printf("    push 0\n");
            return;
        }
        for (Node *n = node->body; n != NULL; n = n->next) {
            gen(n);
            if (n->next != NULL) {
                printf("    pop rax\n");
            }
        }
        return;
        case ND_NOP:
        // Does nothing, but still leaves a value for the statement-level pop.
        printf("    push 0\n");
        return;
        case ND_WHILE: {
            int seq = label_seq++;
            printf(".L.begin.%d:\n", seq);
            gen(node->cond);
            printf("    pop rax\n");
            printf("    cmp rax, 0\n");
            printf("    je .L.end.%d\n", seq);
            gen(node->then);
            // Discard the body's value, or the stack grows by one per
            // iteration.
            printf("    pop rax\n");
            printf("    jmp .L.begin.%d\n", seq);
            printf(".L.end.%d:\n", seq);
            // The loop's own value, so the statement-level pop has one.
            printf("    push 0\n");
            return;
        }
        case ND_FOR: {
            int seq = label_seq++;
            if (node->init != NULL) {
                gen(node->init);
                printf("    pop rax\n");
            }
            printf(".L.begin.%d:\n", seq);
            // An omitted condition is true: fall straight through to the body.
            if (node->cond != NULL) {
                gen(node->cond);
                printf("    pop rax\n");
                printf("    cmp rax, 0\n");
                printf("    je .L.end.%d\n", seq);
            }
            gen(node->then);
            printf("    pop rax\n");
            if (node->inc != NULL) {
                gen(node->inc);
                printf("    pop rax\n");
            }
            printf("    jmp .L.begin.%d\n", seq);
            printf(".L.end.%d:\n", seq);
            printf("    push 0\n");
            return;
        }
        case ND_RETURN:
        gen(node->lhs);
        printf("    pop rax\n");
        // Jump to the single epilogue rather than duplicating it here.
        printf("    jmp %s\n", RETURN_LABEL);
        // The value is not pushed: control never reaches the statement pop.
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
