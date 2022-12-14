#/bin/bash

assert() {
    expected="$1"
    input="$2"

    ./ycc "$input" > temp.s
    cc -o temp temp.s
    ./temp
    actual="$?"

    if [ "$actual" = "$expected" ]; then
        echo "$input => $actual"
    else
        echo "$input => $expected expected, but got $actual"
        exit 1
    fi
}

assert 0 0
assert 22 22
assert 21 "5+20-4"
assert 41 " 12 + 34 - 5 "
assert 47 '5+6*7'
assert 15 '5*(-6+9)'
assert 4 '(3+5)/2'
assert 10 '-10+20'
assert 25 '-20-20+65'
assert 0 '0==1'
assert 1 '42==42'
assert 1 '5 == 1 + 4'
assert 1 '5 * 5 == 25'
assert 1 '(30 - 1) * (30 + 1) == 899'
assert 0 '(30 - 1) * (30 + 1) == 900'
echo OK
