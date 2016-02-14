#include <stdlib.h>
#include <string.h>

#include <GL/gl.h>
#include <GL/glx.h>
#include <GL/glxext.h>

#include "a.h"
#include "d.h"
#include "win.h"


static uint64_t frame_tag;

void d_inc_frame_tag()
{
	frame_tag++;
}

uint64_t d_get_frame_tag()
{
	return frame_tag;
}

void d_texture_init(struct d_texture* t, int channels, int width, int height)
{
	glGenTextures(1, &t->texture); CHKGL;
	int level = 0;
	int border = 0;
	glBindTexture(GL_TEXTURE_2D, t->texture);

	switch (channels) {
		case 1:
			t->format = GL_RED;
			break;
		case 3:
			t->format = GL_RGB;
			break;
		case 4:
			t->format = GL_RGBA;
			break;
		default:
			WRONG("channel count not support");
	}

	t->width = width;
	t->height = height;
	t->channels = channels;

	glTexImage2D(
		GL_TEXTURE_2D,
		level,
		channels,
		width, height,
		border,
		t->format,
		GL_UNSIGNED_BYTE,
		NULL); CHKGL;

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); CHKGL;
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR); CHKGL;
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); CHKGL;
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE); CHKGL;
}

void d_texture_free(struct d_texture* t)
{
	glDeleteTextures(1, &t->texture);
	memset(t, 0, sizeof(*t));
}

void d_texture_clear(struct d_texture* t)
{
	// XXX there are multiple approaches to this that might be better...
	// first of all I ought to use a scratch buffer rather than calloc, but
	// there's also glClearBuffer (attaching as framebuffer) and
	// GL_ARB_clear_texture
	size_t sz = t->width * t->height * t->channels;
	void* tmp = calloc(sz, 1);
	AN(tmp);
	d_texture_sub_image(t, 0, 0, t->width, t->height, tmp);
	free(tmp);
}

void d_texture_sub_image(struct d_texture* t, int x, int y, int w, int h, void* data)
{
	int level = 0;
	glBindTexture(GL_TEXTURE_2D, t->texture);

	int set_align = (t->channels != 4);

	if (set_align) {
		glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	}

	glTexSubImage2D(
		GL_TEXTURE_2D,
		level,
		x, y,
		w, h,
		t->format,
		GL_UNSIGNED_BYTE,
		data); CHKGL;

	if (set_align) {
		glPopClientAttrib();
	}
}

void d_blit(struct d_texture* t, int sx, int sy, int sw, int sh, float dx, float dy)
{
	glEnable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, t->texture);

	glEnable(GL_BLEND);
	//glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); CHKGL;
	glBlendFunc(GL_ONE, GL_ONE); CHKGL; // XXX additive - need to be set elsewhere

	float tsx = (float)sx / (float)t->width;
	float tsy = (float)sy / (float)t->height;
	float tsw = (float)sw / (float)t->width;
	float tsh = (float)sh / (float)t->height;

	glBegin(GL_QUADS);
	glTexCoord2f(tsx, tsy); glVertex2f(dx, dy);
	glTexCoord2f(tsx + tsw, tsy); glVertex2f(dx + sw, dy);
	glTexCoord2f(tsx + tsw, tsy + tsh); glVertex2f(dx + sw, dy + sh);
	glTexCoord2f(tsx, tsy + tsh); glVertex2f(dx, dy + sh);
	glEnd();

}

void d_begin(int win_id)
{
	int width, height;
	win_begin(win_id, &width, &height);
	glMatrixMode(GL_PROJECTION); CHKGL;
	glLoadIdentity(); CHKGL;
	glOrtho(0, width, height, 0, -1, 1); CHKGL;
}

void d_clear()
{
	glClearColor(0.0, 0.1, 0.2, 0.0);
	glClear(GL_COLOR_BUFFER_BIT);
}

