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

    // main is no longer special here: it is just the function the linker
    // happens to start at.
    gen_program();

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
