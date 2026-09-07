#include "ycc.h"

//
// Code Generator
//

// Distinguishes the labels of one control-flow construct from another.
static int label_seq = 0;

// Number of 8-byte values pushed on the path that falls through to here.
// Every gen() leaves exactly one, so this is known at compile time and needs no
// runtime test to decide whether a call site must be padded.
static int depth = 0;

static void push(char *operand) {
    printf("    push %s\n", operand);
    depth++;
}

static void push_int(int val) {
    printf("    push %d\n", val);
    depth++;
}

static void pop(char *reg) {
    printf("    pop %s\n", reg);
    depth--;
}

// True when rsp is 8 mod 16, so a call from here needs padding. The frame is a
// multiple of 16, so alignment depends only on how many values are pushed.
bool stack_misaligned(void) {
    return depth % 2 != 0;
}

int stack_depth(void) {
    return depth;
}

// gen() a node that must leave exactly one value, and verify that it did.
// Checking at each site matters: an if arm rewinds the count before the other
// arm runs, so a miscount there would otherwise be erased and the final total
// would still come out right.
static void gen_one(Node *node) {
    int before = depth;
    gen(node);
    if (depth != before + 1) {
        error("codegen: node kind %d left depth %d, expected %d",
              node->kind, depth, before + 1);
    }
}

// Evaluate both children, leaving the left operand in rax and the right in rdi.
static void gen_operands(Node *node) {
    gen_one(node->lhs);
    gen_one(node->rhs);

    pop("rdi");
    pop("rax");
}

// Push the address of a node that can be assigned to.
static void gen_lval(Node *node) {
    if (node->kind != ND_LVAR) {
        error("codegen: not an lvalue");
    }

    printf("    mov rax, rbp\n");
    printf("    sub rax, %d\n", node->offset);
    push("rax");
}

// Each kind drives its own recursion, so an unknown kind is rejected before
// any child is dereferenced.
void gen(Node *node) {
    if (node == NULL) {
        error("codegen: null node");
    }

    switch (node->kind) {
        case ND_NUM:
        push_int(node->val);
        return;
        case ND_LVAR:
        gen_lval(node);
        pop("rax");
        printf("    mov rax, [rax]\n");
        push("rax");
        return;
        case ND_IF: {
            // Every statement leaves exactly one value for the statement-level
            // pop, so both arms must push. An if with no else and a false
            // condition pushes 0.
            int seq = label_seq++;
            gen_one(node->cond);
            pop("rax");
            printf("    cmp rax, 0\n");
            printf("    je .L.else.%d\n", seq);
            // Both arms leave one value, so they must be counted once, not
            // twice: rewind to the branch point before generating the else.
            int branch_depth = depth;
            gen_one(node->then);
            printf("    jmp .L.end.%d\n", seq);
            printf(".L.else.%d:\n", seq);
            depth = branch_depth;
            if (node->els != NULL) {
                gen_one(node->els);
            } else {
                push_int(0);
            }
            printf(".L.end.%d:\n", seq);
            return;
        }
        case ND_BLOCK:
        // A block's value is its last statement's, so every earlier value is
        // popped and the last one is left for whoever consumes this block.
        if (node->body == NULL) {
            push_int(0);
            return;
        }
        for (Node *n = node->body; n != NULL; n = n->next) {
            gen_one(n);
            if (n->next != NULL) {
                pop("rax");
            }
        }
        return;
        case ND_NOP:
        // Does nothing, but still leaves a value for the statement-level pop.
        push_int(0);
        return;
        case ND_WHILE: {
            int seq = label_seq++;
            printf(".L.begin.%d:\n", seq);
            gen_one(node->cond);
            pop("rax");
            printf("    cmp rax, 0\n");
            printf("    je .L.end.%d\n", seq);
            gen_one(node->then);
            // Discard the body's value, or the stack grows by one per
            // iteration.
            pop("rax");
            printf("    jmp .L.begin.%d\n", seq);
            printf(".L.end.%d:\n", seq);
            // The loop's own value, so the statement-level pop has one.
            push_int(0);
            return;
        }
        case ND_FOR: {
            int seq = label_seq++;
            if (node->init != NULL) {
                gen_one(node->init);
                pop("rax");
            }
            printf(".L.begin.%d:\n", seq);
            // An omitted condition is true: fall straight through to the body.
            if (node->cond != NULL) {
                gen_one(node->cond);
                pop("rax");
                printf("    cmp rax, 0\n");
                printf("    je .L.end.%d\n", seq);
            }
            gen_one(node->then);
            pop("rax");
            if (node->inc != NULL) {
                gen_one(node->inc);
                pop("rax");
            }
            printf("    jmp .L.begin.%d\n", seq);
            printf(".L.end.%d:\n", seq);
            push_int(0);
            return;
        }
        case ND_RETURN:
        gen_one(node->lhs);
        pop("rax");
        // Jump to the single epilogue rather than duplicating it here.
        printf("    jmp %s\n", RETURN_LABEL);
        // Nothing is pushed, because control never falls through to the
        // statement pop. Count one anyway so the caller's accounting matches
        // the unreachable instructions that follow.
        depth++;
        return;
        case ND_ASSIGN:
        gen_lval(node->lhs);
        gen_one(node->rhs);
        pop("rdi");
        pop("rax");
        printf("    mov [rax], rdi\n");
        // Assignment is an expression: its value is the value stored.
        push("rdi");
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

    push("rax");
}

// Emit the whole program, leaving its value in rax.
void gen_program(Node *node) {
    gen_one(node);
    pop("rax");

    // gen_one() has already checked each individual site; this catches anything
    // left over, so a construct that pushes or pops the wrong number of times
    // shows up here rather than as a corrupted return address at runtime.
    if (depth != 0) {
        error("codegen: stack depth is %d at end of program, expected 0", depth);
    }
}
