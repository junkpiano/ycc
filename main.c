#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "ycc.h"

int main(int argc, char *argv[]) {
    if (argc != 2) {
        error("%s: invalid number of arguments", argv[0]);
        return 1;
    }

    init_token(argv[1]);
    program();

    printf(".intel_syntax noprefix\n");
    printf(".globl main\n");
    printf("main:\n");

    // Prologue. The frame size is only known once the whole program is
    // parsed, which is why program() runs before any of this is emitted.
    printf("    push rbp\n");
    printf("    mov rbp, rsp\n");
    printf("    sub rsp, %d\n", frame_size());

    // Each statement leaves its value on the stack. Pop it so the stack stays
    // balanced; the last one popped is the program's value.
    for (int i = 0; code[i] != NULL; i++) {
        gen(code[i]);
        printf("    pop rax\n");
    }

    // Epilogue.
    printf("    mov rsp, rbp\n");
    printf("    pop rbp\n");
    printf("    ret\n");

    return 0;
}

//
// Error reporting
//

// Reports an error and exit.
void error(char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  fprintf(stderr, "\n");
  va_end(ap);
  exit(1);
}

void error_at(char *loc, char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);

    int pos = loc - user_input;
    fprintf(stderr, "%s\n", user_input);
    // Pad with `pos` spaces, then point at the offending token.
    fprintf(stderr, "%*s", pos, "");
    fprintf(stderr, "^ ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    exit(1);
}
