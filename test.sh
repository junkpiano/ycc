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

echo "OK ($pass assertions)"
