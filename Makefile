CC = gcc
CFLAGS=-std=c11 -g3 -static -Wall -Wextra -Wswitch-enum -Wstrict-prototypes -Wmissing-prototypes
SRCS = $(wildcard *.c)
OBJS = $(SRCS:.c=.o)

ycc: $(OBJS)
	$(CC) -o ycc $(OBJS) $(CFLAGS)

$(OBJS): ycc.h

test: ycc
	./test.sh

clean:
	rm -rf ycc *.o *~ temp*

.PHONY: test clean