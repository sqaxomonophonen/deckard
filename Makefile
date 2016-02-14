USE=-DUSE_GL
CFLAGS=-g -O0 -std=c99 -DGLX11 -Wall $(USE)
LINK=-lm -lX11 -lGL -lrt -Wall

all: deckard

a.o: a.c
	$(CC) $(CFLAGS) -c $<

sys_posix.o: sys_posix.c
	$(CC) $(CFLAGS) -c $<

win_glx11.o: win_glx11.c
	$(CC) $(CFLAGS) -c $<

d_gl.o: d_gl.c
	$(CC) $(CFLAGS) -c $<

d_font.o: d_font.c
	$(CC) $(CFLAGS) $(shell pkg-config freetype2 --cflags) -c $<

ui_main.o: ui_main.c
	$(CC) $(CFLAGS) -c $<

deckard: a.o sys_posix.o d_gl.o d_font.o ui_main.o win_glx11.o
	$(CC) $(LINK) $(shell pkg-config freetype2 --libs) $^ -o $@

clean:
	rm -f *.o deckard
