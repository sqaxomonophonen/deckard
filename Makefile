USE=-DUSE_GL
CFLAGS=-g -O0 -std=c99 -DGLX11 -Wall -Igl3w/include $(USE)
LINK=-ldl -lm -lX11 -lGL -lrt -Wall

all: deckard

gl3w.o: gl3w/src/gl3w.c
	$(CC) $(CFLAGS) -c $<

a.o: a.c
	$(CC) $(CFLAGS) -c $<

log.o: log.c
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

deckard: gl3w.o a.o log.o sys_posix.o d_gl.o d_main_atlas.o d_font.o deckard_main.o win_glx11.o
	$(CC) $(LINK) $(shell pkg-config freetype2 --libs) $^ -o $@

clean:
	rm -f *.o deckard
