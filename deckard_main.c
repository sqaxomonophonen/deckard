#include "win.h"
#include "d.h"

int app_main(int argc, char** argv)
{
	win_id main_window = win_open();
	win_make_current(main_window);

	d_reset_glyph_cache(2048, 2048);

	d_set_blend_mode_additive();

	int font_handle = d_open_font("builtin:Aileron-Regular.otf", 10);
	if (font_handle == -1) return 1;

	int exiting = 0;
	while (!exiting) {
		struct win_event e;
		while (win_poll_event(&e)) {
			switch (e.type) {
				case EV_KEYDOWN:
					if (e.key.sym == 'q') exiting = 1;
					break;
				case EV_BUTTONDOWN:
					exiting = 1;
					break;
			}
		}

		d_inc_frame_tag();
		d_begin(main_window);

		d_clear();

		d_text_set_cursor(20.0 + ((float)d_get_frame_tag()) / 32.0, 20 + ((float)d_get_frame_tag()) / 64.0);
		d_printf(font_handle, "hello world HELLO WORLD!!?@#!$^&*( joeqsixpack@gmail.com\nframe tag: %ld", d_get_frame_tag());

		win_flip(main_window);
	}

	d_close_font(font_handle);

	return 0;
}

