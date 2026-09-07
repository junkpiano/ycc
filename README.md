y(usuke) C compiler
====================

*A tiny C compiler*

A small C compiler for x86-64, written for study. It reads an expression as a
single command line argument and writes GNU assembler source (Intel syntax) to
stdout.

## Build

    make

## Usage

`ycc` emits assembly for one expression. Running the result exits with that
expression's value, so only its low 8 bits survive: `300` exits with status 44,
and `-5` with status 251.

    $ ./ycc '5 * (9 - 6)' > temp.s
    $ cc -o temp temp.s
    $ ./temp; echo $?
    15

## Supported grammar

    expr       = equality
    equality   = relational ("==" relational | "!=" relational)*
    relational = add ("<" add | "<=" add | ">" add | ">=" add)*
    add        = mul ("+" mul | "-" mul)*
    mul        = unary ("*" unary | "/" unary)*
    unary      = ("+" | "-") unary | primary
    primary    = num | "(" expr ")"

Integers are the only type. Comparisons yield 1 or 0 and chain to the left, so
`1<2<3` is `(1<2)<3`.

## Test

    make test

`test.sh` compiles each case, assembles and links it with the system `cc`, runs
it, and compares the exit status against the expected value. Expected values are
therefore limited to 0-255.

## Author

Yusuke Ohashi(mail@yusuke.cloud)
