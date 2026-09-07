y(usuke) C compiler
====================

*A tiny C compiler*

A small C compiler for x86-64, written for study. It reads a program as a single
command line argument and writes GNU assembler source (Intel syntax) to stdout.

## Build

    make

Builds with `-Wall -Wextra -Wswitch-enum -Wstrict-prototypes -Wmissing-prototypes`.

`-Wswitch-enum` is deliberate: the code generator's switch has a `default:` that
rejects unknown node kinds at runtime, and that suppresses plain `-Wswitch`, so a
node kind added without a codegen case would otherwise go unreported at compile
time. `-Wstrict-prototypes` keeps `f()` from creeping back in where `f(void)` is
meant -- in C the former declares unspecified parameters rather than none.
`-Wmissing-prototypes` catches a function that is externally visible but has no
declaration, which usually means it should have been `static`.

## Usage

A program is a sequence of function definitions. Execution starts at `main`, and
the program exits with the value it returns, so only the low 8 bits survive:
`300` exits with status 44, and `-5` with status 251.

    $ ./ycc 'add(a,b) { return a+b; } main() { return add(3, 4); }' > temp.s
    $ cc -o temp temp.s
    $ ./temp; echo $?
    7

A function body is a sequence of statements: an expression followed by `;`, a
bare `;`, a `return`, or one of the control-flow forms below. Statements are
evaluated in order and the function returns the value of the last one reached --
or of the first `return` taken, which stops there.

At least one function is required; empty input is rejected.

## Supported grammar

    program    = function+
    function   = ident "(" (ident ("," ident)*)? ")" "{" stmt* "}"
    stmt       = "{" stmt* "}"
               | ";"
               | expr ";"
               | "return" expr ";"
               | "if" "(" expr ")" stmt ("else" stmt)?
               | "while" "(" expr ")" stmt
               | "for" "(" expr? ";" expr? ";" expr? ")" stmt
    expr       = assign
    assign     = equality ("=" assign)?
    equality   = relational ("==" relational | "!=" relational)*
    relational = add ("<" add | "<=" add | ">" add | ">=" add)*
    add        = mul ("+" mul | "-" mul)*
    mul        = unary ("*" unary | "/" unary)*
    unary      = ("+" | "-") unary | primary
    primary    = num
               | ident ("(" (expr ("," expr)*)? ")")?
               | "(" expr ")"

Integers are the only type. Comparisons yield 1 or 0 and
chain to the left, so `1<2<3` is `(1<2)<3`.

`return` ends the program with the given value. Statements after it are still
compiled but never run. Every `return` jumps to a single shared epilogue rather
than carrying its own copy.

## Control flow

    if (a == 1) b = 2; else b = 3;

An `else` binds to the nearest unmatched `if`.

    while (i < 10) i = i + 1;
    for (i = 0; i < 5; i = i + 1) s = s + i;

All three clauses of a `for` are optional. **An omitted condition is true**, so
`for (;;)` loops forever and needs a `return` to escape it.

`;` on its own is the null statement, which is what makes an empty loop body
like `for (i = 0; i < 3; i = i + 1) ;` work.

A block `{ ... }` groups statements and is itself a statement, so it can be a
loop or conditional body:

    for (i = 0; i < 3; i = i + 1) { a = a + i; a = a + 1; }

Blocks do not introduce a scope -- a variable is visible throughout the function
once its name has been seen, though not outside it. That changes with the type work.

A function body is itself a block, so there is no limit on how many statements
it can hold.

Every statement has a value. For an expression statement that is the
expression's value; for an `if` it is the branch taken, or 0 when the condition
is false and there is no `else`; for a loop and for the null
statement it is 0; for a block it is its last statement's value, or 0 if it is
empty.

    $ ./ycc 'a=1; if (a) 7; else 8;' > temp.s   # 7
    $ ./ycc 'if (0) 7;' > temp.s                # 0

There is no `break` or `continue` yet.

## Keywords

`return`, `if`, `else`, `while` and `for`. Each is a keyword only when it is a
whole identifier, so `returnx`, `iffy`, `elsewhere`, `whilst` and `format` are
ordinary variable names.

## Variables

Names are `[A-Za-z_][A-Za-z0-9_]*` and are case sensitive, so `a` and `A` are
different variables. Each name seen gets its own 8-byte slot in the function
frame the first time it appears; there are no declarations, and no fixed limit
on how many there can be. Locals are per function, so two functions each using
`a` get separate slots. Parameters are simply the first locals. A local is not initialised -- reading one before
assigning to it yields whatever is in that slot.

Because a name is created on first use, a misspelling silently becomes a new
variable rather than an error. Declarations arrive with the type work.

Assignment is an expression, and is right associative, so `a = b = 3` assigns 3
to both and evaluates to 3.

    $ ./ycc 'a=3; b=5*6-8; a+b/2;' > temp.s
    $ cc -o temp temp.s
    $ ./temp; echo $?
    14

## Functions

    add(a, b) { return a + b; }

There are no types yet, so a definition is just a name, parameter names, and a
body. Up to six parameters. Each function has its own locals, its own frame and
its own epilogue label, so a name used in one function is unrelated to the same
name in another. Recursion and mutual recursion both work.

`main` is not special to the compiler -- it is simply the function the linker
starts at.

A name followed by `(` is a call. Up to six arguments, passed in `rdi`, `rsi`,
`rdx`, `rcx`, `r8`, `r9` as the System V ABI requires; a seventh is rejected.
Arguments are evaluated left to right.

An `int` result arrives in `eax`, so it is sign-extended into `rax` before use.
Without that a negative return reads as a large positive number in any
comparison, while still looking correct as an exit status — the low byte is the
same either way.

A callee need not be defined here: the test suite links `tests/helper.c` and
calls into it.

    $ ./ycc 'main() { return add2(3, 4); }' > temp.s
    $ cc -o temp temp.s tests/helper.c
    $ ./temp; echo $?
    7

## Stack discipline

Every generated node leaves exactly one 8-byte value on the stack, and the code
generator counts them as it emits. `return` is the one exception, and only on
paper: it jumps to the epilogue instead of falling through, so it pushes
nothing, but is counted as one so that the unreachable instructions after it
still balance in the enclosing accounting.

Two things follow.

A mistake in that accounting is caught at compile time rather than becoming a
corrupted return address at run time. The count is checked at every site that
expects a value, and again at the end of each function. Checking only at the end would not be enough: the two arms of an `if` must each leave one value, so the count is
rewound between them, and a miscount inside one arm would be erased by that
rewind while the total still came out right.

Alignment is known statically. The System V ABI requires `rsp` to be 16-byte
aligned at a `call`. The frame reserved by the prologue is rounded up to a
multiple of 16, so after `push rbp` the base is aligned and whether padding is
needed at a given point depends only on whether the running count is odd. No
runtime test is required.

Calls use this: a call site pads with `sub rsp, 8` exactly when the count is
odd. `tests/helper.c` provides `rsp_aligned()`, which reports whether `rsp` was
aligned at the call, and the suite asserts it from both even and odd depths.
Removing the padding makes the odd-depth case fail, so the assertion is real
rather than decorative.

## Source layout

| File | Contents |
| --- | --- |
| `ycc.h` | Token and node types, and every prototype |
| `tokenize.c` | The tokenizer, and the `consume`/`expect` helpers the parser reads tokens through |
| `parse.c` | Recursive descent over the grammar above, building the AST |
| `codegen.c` | Walks the AST and emits assembly |
| `main.c` | Entry point, and error reporting |
| `tests/helper.c` | External callees the tests link against, including the alignment probe |

## Test

    make test

`test.sh` compiles each case, assembles and links it with the system `cc`, runs
it, and compares the exit status against the expected value. Expected values are
therefore limited to 0-255.

`assert_fail` covers the other direction: the input must be rejected with a
non-zero status and a diagnostic on stderr. The script stops at the first
failure and prints the number of assertions that passed.

## Author

Yusuke Ohashi(mail@yusuke.cloud)
