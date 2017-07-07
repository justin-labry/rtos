.PHONY: run all clean

CFLAGS = -I ../../../lib/include  -I../../../lib/pncp/include -O2 -g -Wall -Werror -m64 -std=gnu99

DIR = obj

OBJS = obj/main.o

LIBS = ../../../lib/libpncp.a ../../../lib/libext.a  ../../../lib/libtlsf.a ../../../lib/libhal.a -lrt

all: $(OBJS)
	gcc -o main $^ $(LIBS)

obj/%.o: src/%.c
	mkdir -p $(DIR)
	gcc $(CFLAGS) -c -o $@ $<

clean:
	rm -rf obj
	rm -f main

run: all
	./run.sh
