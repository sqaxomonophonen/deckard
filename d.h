#ifndef D_H

#ifdef USE_GL
#include <GL/gl.h>
#else
#error "implementation missing"
#endif

struct d_texture {
	int width, height;
	#if USE_GL
	GLuint texture;
	GLenum format;
	int channels;
	#endif
};

void d_texture_init(struct d_texture*, int channels, int width, int height);
void d_texture_free(struct d_texture*);
void d_texture_clear(struct d_texture*);
void d_texture_sub_image(struct d_texture*, int x, int y, int w, int h, void* data);
void d_blit(struct d_texture*, int sx, int sy, int sw, int sh, float dx, float dy);

// counter that increments every frame
void d_inc_frame_tag();
uint64_t d_get_frame_tag();

void d_begin(int win_id);
void d_clear();

void d_set_blend_mode_additive();

// font
void d_reset_glyph_cache(int glyph_cache_width, int glyph_cache_height);

/* returns list of (display_name,font_spec) pairs. elements are
 * null-terminated, and so is the list itself. i.e. a list containing two fonts
 * could look like:
 *     "name1\0spec1\0name2\0spec2\0\0"
 * show display_name to the user. pass font_spec to d_open_font (and save it in
 * config files). return value should not be freed, and is only guaranteed to
 * be valid until the next d_font_get_list() call.
 */
char* d_font_get_list();

/* open font; pass the second element in a (display_name,font_spec) pair from
 * d_font_get_list(). returns font_handle on success, or -1 on error */
int d_open_font(char* font_spec, int size);

/* close font; pass font_handle returned by d_open_font */
void d_close_font(int font_handle);

void d_text_set_cursor(float x, float y);

int d_str(int font_handle, char* str);
int d_printf(int font_handle, const char* fmt, ...) __attribute__ ((format (printf, 2, 3)));

#define D_H
#endif
