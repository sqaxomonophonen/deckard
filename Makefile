USE=-DUSE_GL
OPT=-g -O0
STD=-std=gnu99
CFLAGS=$(OPT) $(STD) -DGLX11 -Wall -Igl3w/include $(USE)
LINK=-ldl -lm -lX11 -lGL -lrt -Wall

.PHONY: unittests

all: deckard

gl3w.o: gl3w/src/gl3w.c
	$(CC) $(CFLAGS) -c $<

a.o: a.c a.h
	$(CC) $(CFLAGS) -c $<

mem.o: mem.c mem.h
	$(CC) $(CFLAGS) -c $<

log.o: log.c
	$(CC) $(CFLAGS) -c $<

slab.o: slab.c slab.h
	$(CC) $(CFLAGS) -c $<

sys_posix.o: sys_posix.c
	$(CC) $(CFLAGS) -c $<

win_glx11.o: win_glx11.c
	$(CC) $(CFLAGS) -c $<

d_gl.o: d_gl.c
	$(CC) $(CFLAGS) -c $<

d_main_atlas.o: d_main_atlas.c
	$(CC) $(CFLAGS) -c $<

d_font.o: d_font.c
	$(CC) $(CFLAGS) $(shell pkg-config freetype2 --cflags) -c $<

deckard_main.o: deckard_main.c
	$(CC) $(CFLAGS) -c $<

deckard: gl3w.o a.o mem.o log.o slab.o sys_posix.o d_gl.o d_main_atlas.o d_font.o deckard_main.o win_glx11.o
	$(CC) $(LINK) $(shell pkg-config freetype2 --libs) $^ -o $@

UNITTESTS=test_slab

clean:
	rm -f *.o deckard $(UNITTESTS)


UNITTEST_CFLAGS=-g -O0 -Wall $(STD) -DUNITTEST

test_slab: slab.c unittest.h
	$(CC) $(UNITTEST_CFLAGS) $< -o $@

unittests: $(UNITTESTS)

runtest=./runtest.sh

run-unittests: unittests
	$(runtest) ./test_slab
