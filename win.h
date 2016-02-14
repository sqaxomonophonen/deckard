#ifndef WIN_H

#ifdef GLX11
#include <X11/Xlib.h>
typedef Window win_id;
#endif

win_id win_open();

void win_make_current(win_id id);

#define EV_KEYDOWN (1)
#define EV_BUTTONDOWN (2)
#define EV_BUTTONUP (3)


struct win_event {
	int type;
	union {
		struct {
			int sym;
		} key;

		struct {
		} button;

		struct {
		} move;
	};
};

void win_begin(win_id id, int* width, int* height);
int win_poll_event(struct win_event* e);

void win_flip(win_id id);

#define WIN_H
#endif
