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

A program is a sequence of statements: an expression followed by `;`, a bare `;`,
a `return`, or one of the control-flow forms below. Statements are evaluated in
order and the program takes the value of the last one reached -- or of the first
`return` taken, which stops there. Running the
result exits with that value, so only its low 8 bits survive: `300` exits with
status 44, and `-5` with status 251.

    $ ./ycc '1 + 2; 5 * (9 - 6);' > temp.s
    $ cc -o temp temp.s
    $ ./temp; echo $?
    15

At least one statement is required; empty input is rejected.

## Supported grammar

    program    = stmt+
    stmt       = ";"
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
    primary    = num | ident | "(" expr ")"

At most 100 top-level statements -- statements nested inside a loop or a
conditional do not count towards that. Integers are the only type. Comparisons yield 1 or 0 and
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

Every statement has a value. For an expression statement that is the
expression's value; for an `if` it is the branch taken, or 0 when the condition
is false and there is no `else`; for a loop and for the null statement it is 0.

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
on how many there can be. A local is not initialised -- reading one before
assigning to it yields whatever is in that slot.

Because a name is created on first use, a misspelling silently becomes a new
variable rather than an error. Declarations arrive with the type work.

Assignment is an expression, and is right associative, so `a = b = 3` assigns 3
to both and evaluates to 3.

    $ ./ycc 'a=3; b=5*6-8; a+b/2;' > temp.s
    $ cc -o temp temp.s
    $ ./temp; echo $?
    14

## Source layout

| File | Contents |
| --- | --- |
| `ycc.h` | Token and node types, and every prototype |
| `tokenize.c` | The tokenizer, and the `consume`/`expect` helpers the parser reads tokens through |
| `parse.c` | Recursive descent over the grammar above, building the AST |
| `codegen.c` | Walks the AST and emits assembly |
| `main.c` | Entry point, and error reporting |

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
