#include <string.h>

#include "ycc.h"

//
// Code Generator
//

// Distinguishes the labels of one control-flow construct from another.
static int label_seq = 0;

// Is this call to a function defined in this program?
//
// It matters because of what the result register means. A C function returning
// int leaves it in eax, so the upper half of rax is undefined and has to be
// sign-extended. A function defined here returns a full 64-bit value in rax,
// and sign-extending that would truncate any address it returns.
static bool defined_here(Node *call) {
    for (Function *fn = functions; fn != NULL; fn = fn->next) {
        if (fn->name_len == call->funcname_len &&
            memcmp(fn->name, call->funcname, fn->name_len) == 0) {
            return true;
        }
    }

    return false;
}

// The function being emitted, for its return label.
static Function *current_fn;

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
    // The lvalue of *e is the value of e, which is what makes "*p = x" work.
    if (node->kind == ND_DEREF) {
        gen_one(node->lhs);
        return;
    }

    if (node->kind != ND_LVAR) {
        error("codegen: not an lvalue");
    }

    // A global is addressed by name; a local by its distance below rbp.
    if (node->var->is_global) {
        printf("    lea rax, %.*s[rip]\n", node->var->len, node->var->name);
    } else {
        printf("    mov rax, rbp\n");
        printf("    sub rax, %d\n", node->var->offset);
    }
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
        // An array used as a value is a pointer to its first element, so the
        // address it already has on the stack is the value. Anything else
        // loads through that address.
        if (!is_array(node->ty)) {
            pop("rax");
            printf("    mov rax, [rax]\n");
            push("rax");
        }
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
        case ND_FUNCALL: {
            static char *argregs[MAX_ARGS] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};

            // Evaluate left to right, then pop in reverse so the first
            // argument ends up in the first register.
            for (Node *arg = node->args; arg != NULL; arg = arg->next) {
                gen_one(arg);
            }
            for (int i = node->nargs - 1; i >= 0; i--) {
                pop(argregs[i]);
            }

            // The ABI wants rsp 16-byte aligned at the call. The frame is a
            // multiple of 16, so this is decided by the count, not a runtime
            // test.
            bool pad = stack_misaligned();
            if (pad) {
                printf("    sub rsp, 8\n");
            }
            // Number of vector registers used, which a variadic callee reads.
            printf("    mov rax, 0\n");
            printf("    call %.*s\n", node->funcname_len, node->funcname);
            if (pad) {
                printf("    add rsp, 8\n");
            }
            // An external int result comes back in eax, leaving the top half
            // of rax undefined, so it must be sign-extended or a negative
            // return reads as a large positive number. A function defined here
            // returns a full 64-bit value, and extending that would truncate a
            // returned address.
            if (!defined_here(node)) {
                printf("    movsx rax, eax\n");
            }
            push("rax");
            return;
        }
        case ND_ADDR:
        // Exactly the lvalue of the operand.
        gen_lval(node->lhs);
        return;
        case ND_DEREF:
        gen_one(node->lhs);
        // Same rule: dereferencing to an array yields its address.
        if (!is_array(node->ty)) {
            pop("rax");
            printf("    mov rax, [rax]\n");
            push("rax");
        }
        return;
        case ND_SIZEOF:
        // add_type() rewrites these into constants, so reaching codegen means
        // the typing pass did not run over this node.
        error("codegen: sizeof was not folded");
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
        printf("    jmp .L.return.%.*s\n", current_fn->name_len, current_fn->name);
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

static void gen_function(Function *fn) {
    static char *argregs[MAX_ARGS] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};

    current_fn = fn;
    depth = 0;

    printf(".globl %.*s\n", fn->name_len, fn->name);
    printf("%.*s:\n", fn->name_len, fn->name);

    // Prologue. The frame size is known only now that the body is parsed.
    printf("    push rbp\n");
    printf("    mov rbp, rsp\n");
    printf("    sub rsp, %d\n", fn->frame_size);

    // Parameters are the first locals. fn->params is newest-first, so walk it
    // backwards through the argument registers.
    int i = fn->nparams - 1;
    for (LVar *param = fn->params; param != NULL && i >= 0; param = param->next, i--) {
        printf("    mov [rbp-%d], %s\n", param->offset, argregs[i]);
    }

    gen_one(fn->body);
    pop("rax");

    // gen_one() has already checked each individual site; this catches anything
    // left over, so a construct that pushes or pops the wrong number of times
    // shows up here rather than as a corrupted return address at runtime.
    if (depth != 0) {
        error("codegen: stack depth is %d at end of %.*s, expected 0",
              depth, fn->name_len, fn->name);
    }

    // One epilogue per function, which every return in it jumps to.
    printf(".L.return.%.*s:\n", fn->name_len, fn->name);
    printf("    mov rsp, rbp\n");
    printf("    pop rbp\n");
    printf("    ret\n");
}

// Globals live in .data and are zeroed, unlike locals.
static void gen_globals(void) {
    if (globals == NULL) {
        return;
    }

    printf(".data\n");
    for (LVar *var = globals; var != NULL; var = var->next) {
        printf(".globl %.*s\n", var->len, var->name);
        printf("%.*s:\n", var->len, var->name);
        printf("    .zero %d\n", type_size(var->ty));
    }
    printf(".text\n");
}

void gen_program(void) {
    // Type the whole program first: a call's type can depend on a definition
    // that appears later in the file.
    for (Function *fn = functions; fn != NULL; fn = fn->next) {
        add_type(fn->body);
    }

    gen_globals();

    for (Function *fn = functions; fn != NULL; fn = fn->next) {
        gen_function(fn);
    }
}
