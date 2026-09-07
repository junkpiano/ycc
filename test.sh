#!/bin/bash

pass=0

# Functions the compiled programs call. Built once rather than per assertion.
HELPER=temp-helper.o
cc -c -o "$HELPER" tests/helper.c || exit 1

# Compile, assemble and run the input, and compare the exit status.
assert() {
    expected="$1"
    input="$2"

    # Remove the previous binary first, so a failed build cannot leave a stale
    # one behind for us to run.
    rm -f temp temp.s
    if ! ./ycc "$input" > temp.s; then
        echo "$input => expected $expected, but ycc rejected it"
        rm -f temp.s
        exit 1
    fi
    if ! cc -o temp temp.s "$HELPER"; then
        echo "$input => generated assembly did not build"
        rm -f temp temp.s
        exit 1
    fi
    ./temp
    actual="$?"
    rm -f temp temp.s

    if [ "$actual" = "$expected" ]; then
        echo "$input => $actual"
        pass=$((pass + 1))
    else
        echo "$input => $expected expected, but got $actual"
        exit 1
    fi
}

# Assert that ycc rejects the input: non-zero status and a diagnostic.
assert_fail() {
    input="$1"

    rm -f temp.s temp.err
    ./ycc "$input" > temp.s 2> temp.err
    status="$?"

    if [ "$status" -eq 0 ]; then
        echo "$input => expected a rejection, but it compiled"
        rm -f temp.s temp.err
        exit 1
    fi
    # A signal is a crash, not a rejection. The shell reports one as 128+signo,
    # and would otherwise satisfy both checks below.
    if [ "$status" -ge 128 ]; then
        echo "$input => ycc died on signal $((status - 128))"
        rm -f temp.s temp.err
        exit 1
    fi
    if [ ! -s temp.err ]; then
        echo "$input => rejected without a diagnostic"
        rm -f temp.s temp.err
        exit 1
    fi
    echo "$input => rejected: $(tail -n 1 temp.err)"
    pass=$((pass + 1))
    rm -f temp.s temp.err
}

assert 0 'main() { 0; }'
assert 22 'main() { 22; }'
assert 21 "main() { 5+20-4; }"
assert 41 "main() {  12 + 34 - 5 ; }"
assert 47 'main() { 5+6*7; }'
assert 15 'main() { 5*(-6+9); }'
assert 4 'main() { (3+5)/2; }'
assert 10 'main() { -10+20; }'
assert 25 'main() { -20-20+65; }'
assert 0 'main() { 0==1; }'
assert 1 'main() { 42==42; }'
assert 1 'main() { 5 == 1 + 4; }'
assert 1 'main() { 5 * 5 == 25; }'
assert 1 'main() { (30 - 1) * (30 + 1) == 899; }'
assert 0 'main() { (30 - 1) * (30 + 1) == 900; }'
assert 1 'main() { 1 != 2; }'
assert 0 'main() { 2 != 2; }'
assert 1 'main() { 1 < 2; }'
assert 0 'main() { 2 < 2; }'
assert 1 'main() { 2 <= 2; }'
assert 0 'main() { 3 <= 2; }'
assert 1 'main() { 2 > 1; }'
assert 0 'main() { 2 > 2; }'
assert 1 'main() { 2 >= 2; }'
assert 0 'main() { 2 >= 3; }'
# Chained comparisons. Each of these differs from the value of its first
# comparison alone, so they fail if the parser stops after one operator.
assert 0 'main() { 1<2<1; }'
assert 0 'main() { 1>0>1; }'
assert 0 'main() { 1==1==0; }'
assert 0 'main() { 1!=2!=1; }'
assert 0 'main() { 2<=2<=0; }'
assert 0 'main() { 2>=2>=2; }'
assert 1 'main() { 1<2<3; }'
assert 0 'main() { 1>2>3; }'
assert 1 'main() { 1==1==1; }'
assert 1 'main() { 3>2<2; }'
assert 1 'main() { 3>=2<2; }'
# Nested unary signs. -5 as an exit status is 251.
assert 5 'main() { - -5; }'
assert 5 'main() { - - 5; }'
assert 251 'main() { -+5; }'
assert 251 'main() { +-5; }'
assert 251 'main() { - - -5; }'
assert 5 'main() { -(-5); }'
assert 30 'main() { - -10+20; }'

# Statements. Every statement is evaluated; the program takes the value of the
# last one.
assert 3 'main() { 1; 2; 3; }'
assert 6 'main() { 1+1; 2*3; }'
assert 1 'main() { 2*3; 1<2; }'
assert 5 'main() {  1 ; 5 ;  }'

# Local variables. Single letters a-z, each with its own stack slot.
assert 1 'main() { a=1; a; }'
assert 3 'main() { a=1; b=2; a+b; }'
assert 6 'main() { a=b=3; a+b; }'
assert 5 'main() { a=1; a=a+4; a; }'
assert 25 'main() { z=5; z*z; }'
assert 14 'main() { a=3; b=5*6-8; a+b/2; }'
assert 1 'main() { a=1; z=2; a; }'
assert 2 'main() { a=1; z=2; z; }'
assert 4 'main() { a=1; a=a+1; a=a*2; a; }'
assert 1 'main() { a = 1 ; a ; }'
# Multi-character names, including ones that are prefixes of each other.
assert 3 'main() { foo=1; bar=2; foo+bar; }'
assert 25 'main() { abc=5; abc*abc; }'
assert 1 'main() { ab=1; ac=2; ab; }'
assert 2 'main() { ab=1; ac=2; ac; }'
assert 1 'main() { a=1; ab=2; a; }'
assert 2 'main() { a=1; ab=2; ab; }'
assert 3 'main() { _x=3; _x; }'
assert 4 'main() { x1=4; x1; }'
assert 7 'main() { Foo=7; Foo; }'
assert 9 'main() { a=1; A=9; A; }'
assert 1 'main() { a=1; A=9; a; }'
assert 30 'main() { alpha=10; beta=20; alpha+beta; }'
# More than the 26 slots the previous fixed frame allowed.
assert 39 'main() { v0=0;v1=1;v2=2;v3=3;v4=4;v5=5;v6=6;v7=7;v8=8;v9=9;v10=10;v11=11;v12=12;v13=13;v14=14;v15=15;v16=16;v17=17;v18=18;v19=19;v20=20;v21=21;v22=22;v23=23;v24=24;v25=25;v26=26;v27=27;v28=28;v29=29;v30=30;v31=31;v32=32;v33=33;v34=34;v35=35;v36=36;v37=37;v38=38;v39=39;v39; }'

# Assignment is an expression and is right associative.
assert 7 'main() { a=b=7; b; }'
assert 3 'main() { a=(b=3); a; }'

# return jumps to the single epilogue; later statements are dead.
assert 5 'main() { return 5; }'
assert 3 'main() { a=3; return a; return 9; }'
assert 1 'main() { return 1; return 2; }'
assert 5 'main() { a=1; return a+4; }'
assert 14 'main() { a=3; b=5*6-8; return a+b/2; }'
assert 1 'main() { a=1; return a; a=2; }'
# A keyword is only a keyword when it is the whole identifier.
assert 4 'main() { returnx=4; returnx; }'
assert 7 'main() { return_=7; return_; }'
assert 6 'main() { returns=2; return returns*3; }'
assert 8 'main() { returnvalue=8; return returnvalue; }'

# if / else.
assert 2 'main() { if (1) return 2; return 3; }'
assert 3 'main() { if (0) return 2; return 3; }'
assert 2 'main() { if (1) return 2; else return 3; }'
assert 3 'main() { if (0) return 2; else return 3; }'
assert 5 'main() { a=0; if (1) a=5; return a; }'
assert 0 'main() { a=0; if (0) a=5; return a; }'
assert 1 'main() { a=0; if (1-1) a=5; else a=1; return a; }'
assert 4 'main() { if (1) if (1) if (1) return 4; return 5; }'
assert 2 'main() { a=1; if (a==1) a=2; return a; }'
assert 7 'main() { a=1; if (a==2) a=9; return 7; }'
# A dangling else binds to the nearest if.
assert 2 'main() { if (1) if (0) return 1; else return 2; return 3; }'
assert 3 'main() { if (0) if (0) return 1; else return 2; return 3; }'
# A nested if's value propagates outward, but a later return still wins.
assert 6 'main() { if (1) if (0) return 4; else 6; }'
assert 9 'main() { if (1) if (0) return 4; else 6; return 9; }'
assert 9 'main() { a=1; if (a==1) if (a==2) return 8; else return 9; return 7; }'
# An if is a statement and still leaves one value; with no else and a false
# condition that value is 0.
assert 7 'main() { if (1) 7; }'
assert 0 'main() { if (0) 7; }'
assert 2 'main() { a=1; if (a) a=2; else a=3; if (0) 9; a; }'
# Keywords are only keywords as whole identifiers.
assert 3 'main() { iffy=3; iffy; }'
assert 4 'main() { elsewhere=4; elsewhere; }'
assert 5 'main() { whilst=5; whilst; }'
assert 6 'main() { format=6; format; }'

# while and for.
assert 10 'main() { i=0; while (i<10) i=i+1; return i; }'
assert 3 'main() { i=0; while (i<3) i=i+1; i; }'
assert 5 'main() { i=5; while (0) i=1; return i; }'
assert 10 'main() { a=0; for (i=0; i<5; i=i+1) a=a+i; return a; }'
assert 3 'main() { for (i=0; i<3; i=i+1) ; return i; }'
assert 4 'main() { i=0; for (; i<4; i=i+1) 1; return i; }'
assert 9 'main() { i=0; for (i=9;;) return i; }'
assert 7 'main() { a=1; for (;;) return 7; }'
assert 55 'main() { s=0; for (i=1; i<11; i=i+1) s=s+i; return s; }'
assert 12 'main() { s=0; for (i=0; i<500000; i=i+1) s=s+1; return s-499988; }'
# A loop is a statement and leaves a value of 0, like an if with no else.
assert 0 'main() { i=0; while (i<3) i=i+1; }'
assert 0 'main() { for (i=0; i<3; i=i+1) 9; }'
# The null statement.
assert 0 'main() { ; }'
assert 1 'main() { a=1; ; ; return a; }'
assert 0 'main() { if (1); }'
# There is no statement limit any more: the top level is an implicit block.
assert 5 'main() { 1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;return 5; }'

# Blocks.
assert 3 'main() { { 1; 2; return 3; } }'
assert 2 'main() { if (1) { a=2; return a; } return 9; }'
assert 9 'main() { if (0) { a=2; return a; } return 9; }'
assert 10 'main() { i=0; while (i<10) { i=i+1; } return i; }'
assert 6 'main() { a=0; for (i=0;i<3;i=i+1) { a=a+i; a=a+1; } return a; }'
assert 3 'main() { i=0; for (;;) { i=i+1; if (i==3) return i; } }'
assert 3 'main() { a=1; { a=2; { a=3; } } return a; }'
assert 7 'main() { {{{{{{ 7; }}}}}} }'
# A block's value is its last statement's; an empty block is 0.
assert 2 'main() { { 1; 2; } }'
assert 0 'main() { {} }'
assert 0 'main() { a=1; {}  }'
assert 5 'main() { { 1; { 2; { 5; } } } }'
# Blocks in a loop body must not leak stack across iterations.
assert 12 'main() { s=0; for (i=0; i<300000; i=i+1) { s=s+1; s=s+1; } return s-599988; }'

# Function calls. The callees live in tests/helper.c.
assert 3 'main() { return ret3(); }'
assert 7 'main() { return ret7(); }'
assert 10 'main() { return ret3() + ret7(); }'
assert 7 'main() { return add2(3, 4); }'
assert 1 'main() { return sub2(4, 3); }'
assert 21 'main() { return add6(1, 2, 3, 4, 5, 6); }'
assert 18 'main() { return add6(1, 2, 3, 4, 5, ret3()); }'
assert 21 'main() { a=1; b=2; return add6(a, b, 3, 4, 5, 6); }'
assert 8 'main() { return add2(add2(1, 2), add2(2, 3)); }'
assert 3 'main() { i=0; while (i<3) i=i+1; return ret3(); }'
assert 6 'main() { s=0; for (i=0; i<2; i=i+1) s=s+ret3(); return s; }'
assert 3 'main() { if (1) return ret3(); return 9; }'
# An int result arrives in eax, so it must be sign-extended into rax. Comparing
# only exit statuses would not catch this: -1 and 4294967295 share a low byte.
assert 1 'main() { return sub2(3, 4) < 0; }'
assert 1 'main() { return sub2(3, 4) == 0-1; }'
assert 10 'main() { return sub2(3, 4) + 11; }'
assert 1 'main() { return 0 - sub2(3, 4); }'
assert 1 'main() { return sub2(3, 4) < sub2(4, 3); }'
assert 0 'main() { return sub2(4, 3) < 0; }'
# rsp must be 16-byte aligned at a call. rsp_aligned() reports whether it was.
# The second case calls from an odd stack depth, which needs the padding.
assert 1 'main() { return rsp_aligned(); }'
assert 1 'main() { return 0 + rsp_aligned(); }'
assert 1 'main() { return 0 + (0 + rsp_aligned()); }'
assert 2 'main() { a=1; return a + rsp_aligned(); }'
assert 1 'main() { return add2(rsp_aligned(), 0); }'
assert 1 'main() { return add6(1, 2, 3, 4, 5, rsp_aligned()) - 15; }'

# Function definitions.
assert 3 'main() { return 3; }'
assert 7 'add(a,b) { return a+b; } main() { return add(3,4); }'
assert 1 'sub(a,b) { return a-b; } main() { return sub(4,3); }'
assert 34 'fib(n) { if (n<2) return n; return fib(n-1)+fib(n-2); } main() { return fib(9); }'
assert 55 'fib(n) { if (n<2) return n; return fib(n-1)+fib(n-2); } main() { return fib(10); }'
assert 120 'fact(n) { if (n==0) return 1; return n*fact(n-1); } main() { return fact(5); }'
assert 21 'six(a,b,c,d,e,f) { return a+b+c+d+e+f; } main() { return six(1,2,3,4,5,6); }'
assert 6 'id(x) { return x; } main() { return id(1)+id(2)+id(3); }'
# Each function has its own locals; a name in one does not touch another's.
assert 3 'f() { a=1; return a; } g() { a=2; return a; } main() { return f()+g(); }'
assert 1 'f() { a=9; return 0; } main() { a=1; f(); return a; }'
# Each function has its own frame and its own epilogue label.
assert 4 'f() { a=1; b=2; c=3; return a+b+c; } g() { return 2; } main() { return f()-g(); }'
assert 2 'f() { if (1) return 1; return 9; } main() { return f()+f(); }'
# A function with no locals at all.
assert 4 'four() { return 4; } main() { return four(); }'
# Parameters are ordinary locals and can be assigned to.
assert 10 'f(a) { a = a * 2; return a; } main() { return f(5); }'
assert 5 'f(a,b) { a = a + b; b = a + b; return b; } main() { return f(1,2); }'
# Mutual recursion.
assert 1 'is_even(n) { if (n==0) return 1; return is_odd(n-1); } is_odd(n) { if (n==0) return 0; return is_even(n-1); } main() { return is_even(10); }'
assert 0 'is_even(n) { if (n==0) return 1; return is_odd(n-1); } is_odd(n) { if (n==0) return 0; return is_even(n-1); } main() { return is_even(7); }'
# Calls into C still work, and rsp is still aligned inside a defined function.
assert 3 'main() { return ret3(); }'
assert 1 'f() { return rsp_aligned(); } main() { return f(); }'
assert 1 'f() { return 0 + rsp_aligned(); } main() { return f(); }'
assert 1 'f(a,b,c,d,e,g) { return rsp_aligned(); } main() { return f(1,2,3,4,5,6); }'

# Malformed input must be rejected, not silently miscompiled.
assert_fail 'main() { 1+; }'
assert_fail 'main() { (1; }'
assert_fail 'main() { 1 2; }'
assert_fail 'main() { 1==1 hoge; }'
assert_fail '@;'
assert_fail 'main() { *3; }'
assert_fail 'main() { ); }'
assert_fail ''
assert_fail 'main() { 1 }'
assert_fail 'main() { 1; 2 }'
assert_fail 'main() { 1=2; }'
assert_fail 'main() { a+b=3; }'
assert_fail 'main() { a=; }'
assert_fail 'main() { =1; }'
assert_fail 'main() { return; }'
assert_fail 'main() { return }'
assert_fail 'main() { return return 1; }'
assert_fail 'main() { if 1) return 2; }'
assert_fail 'main() { if (1 return 2; }'
assert_fail 'main() { if () return 1; }'
assert_fail 'main() { else return 1; }'
assert_fail 'main() { while 1) 2; }'
assert_fail 'main() { while (1 2; }'
assert_fail 'main() { while () 2; }'
assert_fail 'main() { for i=0; i<3; i=i+1) 2; }'
assert_fail 'main() { for (i=0; i<3; i=i+1 2; }'
assert_fail 'main() { for (i=0) 2; }'
assert_fail 'main() { for (i=0; i<3) 2; }'
assert_fail 'main() { { 1; }'
assert_fail 'main() { { }'
assert_fail 'main() { } }'
assert_fail 'main() { { 1; } } }'
assert_fail 'main() { if (1) { 2; }'
assert_fail 'main() { return add6(1,2,3,4,5,6,7); }'
assert_fail 'main() { return ret3(; }'
assert_fail 'main() { return ret3(1,); }'
assert_fail 'main() { return ret3(,1); }'
# Malformed function definitions.
assert_fail 'main() { return 1;'
assert_fail 'main( { return 1; }'
assert_fail 'main) { return 1; }'
assert_fail 'main() return 1;'
assert_fail '1() { return 1; }'
assert_fail 'main(a,) { return a; }'
assert_fail 'main(1) { return 1; }'
assert_fail 'main(a,a) { return a; }'
assert_fail 'f(a,b,c,d,e,g,h) { return a; } main() { return 1; }'
assert_fail '() { return 1; }'

rm -f "$HELPER"
echo "OK ($pass assertions)"
