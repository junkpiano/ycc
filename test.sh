#!/bin/bash

pass=0

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
    if ! cc -o temp temp.s; then
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

assert 0 '0;'
assert 22 '22;'
assert 21 "5+20-4;"
assert 41 " 12 + 34 - 5 ;"
assert 47 '5+6*7;'
assert 15 '5*(-6+9);'
assert 4 '(3+5)/2;'
assert 10 '-10+20;'
assert 25 '-20-20+65;'
assert 0 '0==1;'
assert 1 '42==42;'
assert 1 '5 == 1 + 4;'
assert 1 '5 * 5 == 25;'
assert 1 '(30 - 1) * (30 + 1) == 899;'
assert 0 '(30 - 1) * (30 + 1) == 900;'
assert 1 '1 != 2;'
assert 0 '2 != 2;'
assert 1 '1 < 2;'
assert 0 '2 < 2;'
assert 1 '2 <= 2;'
assert 0 '3 <= 2;'
assert 1 '2 > 1;'
assert 0 '2 > 2;'
assert 1 '2 >= 2;'
assert 0 '2 >= 3;'
# Chained comparisons. Each of these differs from the value of its first
# comparison alone, so they fail if the parser stops after one operator.
assert 0 '1<2<1;'
assert 0 '1>0>1;'
assert 0 '1==1==0;'
assert 0 '1!=2!=1;'
assert 0 '2<=2<=0;'
assert 0 '2>=2>=2;'
assert 1 '1<2<3;'
assert 0 '1>2>3;'
assert 1 '1==1==1;'
assert 1 '3>2<2;'
assert 1 '3>=2<2;'
# Nested unary signs. -5 as an exit status is 251.
assert 5 '- -5;'
assert 5 '- - 5;'
assert 251 '-+5;'
assert 251 '+-5;'
assert 251 '- - -5;'
assert 5 '-(-5);'
assert 30 '- -10+20;'

# Statements. Every statement is evaluated; the program takes the value of the
# last one.
assert 3 '1; 2; 3;'
assert 6 '1+1; 2*3;'
assert 1 '2*3; 1<2;'
assert 5 ' 1 ; 5 ; '

# Local variables. Single letters a-z, each with its own stack slot.
assert 1 'a=1; a;'
assert 3 'a=1; b=2; a+b;'
assert 6 'a=b=3; a+b;'
assert 5 'a=1; a=a+4; a;'
assert 25 'z=5; z*z;'
assert 14 'a=3; b=5*6-8; a+b/2;'
assert 1 'a=1; z=2; a;'
assert 2 'a=1; z=2; z;'
assert 4 'a=1; a=a+1; a=a*2; a;'
assert 1 'a = 1 ; a ;'
# Multi-character names, including ones that are prefixes of each other.
assert 3 'foo=1; bar=2; foo+bar;'
assert 25 'abc=5; abc*abc;'
assert 1 'ab=1; ac=2; ab;'
assert 2 'ab=1; ac=2; ac;'
assert 1 'a=1; ab=2; a;'
assert 2 'a=1; ab=2; ab;'
assert 3 '_x=3; _x;'
assert 4 'x1=4; x1;'
assert 7 'Foo=7; Foo;'
assert 9 'a=1; A=9; A;'
assert 1 'a=1; A=9; a;'
assert 30 'alpha=10; beta=20; alpha+beta;'
# More than the 26 slots the previous fixed frame allowed.
assert 39 'v0=0;v1=1;v2=2;v3=3;v4=4;v5=5;v6=6;v7=7;v8=8;v9=9;v10=10;v11=11;v12=12;v13=13;v14=14;v15=15;v16=16;v17=17;v18=18;v19=19;v20=20;v21=21;v22=22;v23=23;v24=24;v25=25;v26=26;v27=27;v28=28;v29=29;v30=30;v31=31;v32=32;v33=33;v34=34;v35=35;v36=36;v37=37;v38=38;v39=39;v39;'

# Assignment is an expression and is right associative.
assert 7 'a=b=7; b;'
assert 3 'a=(b=3); a;'

# return jumps to the single epilogue; later statements are dead.
assert 5 'return 5;'
assert 3 'a=3; return a; return 9;'
assert 1 'return 1; return 2;'
assert 5 'a=1; return a+4;'
assert 14 'a=3; b=5*6-8; return a+b/2;'
assert 1 'a=1; return a; a=2;'
# A keyword is only a keyword when it is the whole identifier.
assert 4 'returnx=4; returnx;'
assert 7 'return_=7; return_;'
assert 6 'returns=2; return returns*3;'
assert 8 'returnvalue=8; return returnvalue;'

# Malformed input must be rejected, not silently miscompiled.
assert_fail '1+;'
assert_fail '(1;'
assert_fail '1 2;'
assert_fail '1==1 hoge;'
assert_fail '@;'
assert_fail '*3;'
assert_fail ');'
assert_fail ''
assert_fail '1'
assert_fail '1; 2'
assert_fail ';'
assert_fail '1=2;'
assert_fail 'a+b=3;'
assert_fail 'a=;'
assert_fail '=1;'
assert_fail 'return;'
assert_fail 'return'
assert_fail 'return return 1;'

echo "OK ($pass assertions)"
