CC = gcc
CFLAGS=-std=c11 -g3 -static

ycc: ycc.c

test: ycc
	./test.sh

clean:
	rm -rf ycc *.o *~ temp*

.PHONY: test clean