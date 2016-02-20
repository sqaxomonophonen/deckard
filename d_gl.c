#include <stdlib.h>
#include <string.h>

#include "gl.h"

#include "scratch.h"
#include "a.h"
#include "d.h"
#include "win.h"

struct draw_vertex {
	union vec2 position;
	union vec2 uv;
	union vec4 color;
};

struct texture_batch {
	int n_elements;
	GLuint texture;
};


// NOTE ElementType and ELEMENT_SIZE_GL *must* match
typedef GLushort ElementType;
#define ELEMENT_SIZE_GL (GL_UNSIGNED_SHORT)
#define MAX_VERTICES (1<<16)
#define MAX_ELEMENTS (1<<17)
#define MAX_TEXTURE_BINDS (1<<12)

static uint64_t frame_tag;

static struct {
	GLuint prg;
	GLuint u_texture;
	GLuint u_scaling;
	GLuint vertex_buffer;
	GLuint vertex_array;
	GLuint element_buffer;

	int n_vertices;
	struct draw_vertex* vertices;

	int n_elements;
	ElementType* elements;

	int n_texture_batches;
	struct texture_batch* texture_batches;
} draw_res;

static struct {
	int begun;
	int win_id;
	int win_width;
	int win_height;
	uint64_t tag;
	union vec4 color0, color1;
} draw_scope;

static GLuint create_shader(const char* src, GLenum type)
{
	GLuint shader = glCreateShader(type); CHKGL;
	glShaderSource(shader, 1, &src, 0); CHKGL;
	glCompileShader(shader);

	GLint status;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
	if (status == GL_FALSE) {
		GLint msglen;
		glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &msglen);
		GLchar* msg = malloc(msglen + 1);
		AN(msg);
		glGetShaderInfoLog(shader, msglen, NULL, msg);
		const char* stype = type == GL_VERTEX_SHADER ? "vertex" : type == GL_FRAGMENT_SHADER ? "fragment" : "waaaat";
		arghf("%s shader compile error: %s -- source:\n%s", stype, msg, src);
	}

	return shader;
}

static GLuint create_program(const char* vert_src, const char* frag_src)
{
	GLuint vert_shader = create_shader(vert_src, GL_VERTEX_SHADER);
	GLuint frag_shader = create_shader(frag_src, GL_FRAGMENT_SHADER);

	GLuint prg = glCreateProgram(); CHKGL;

	glAttachShader(prg, vert_shader); CHKGL;
	glAttachShader(prg, frag_shader); CHKGL;

	glLinkProgram(prg);

	GLint status;
	glGetProgramiv(prg, GL_LINK_STATUS, &status);
	if (status == GL_FALSE) {
		GLint msglen;
		glGetProgramiv(prg, GL_INFO_LOG_LENGTH, &msglen);
		GLchar* msg = (GLchar*) malloc(msglen + 1);
		AN(msg);
		glGetProgramInfoLog(prg, msglen, NULL, msg);
		arghf("shader link error: %s", msg);
	}

	glDeleteShader(vert_shader);
	glDeleteShader(frag_shader);

	return prg;
}

static void draw_flush()
{
	if (!draw_res.n_vertices || !draw_res.n_elements || !draw_res.n_texture_batches) {
		// nothing to do
		return;
	}

	glBindBuffer(GL_ARRAY_BUFFER, draw_res.vertex_buffer);
	glBufferSubData(GL_ARRAY_BUFFER, 0, draw_res.n_vertices * sizeof(struct draw_vertex), draw_res.vertices);

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, draw_res.element_buffer);
	glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, draw_res.n_elements * sizeof(ElementType), draw_res.elements);

	ElementType* offset = 0;
	for (int i = 0; i < draw_res.n_texture_batches; i++) {
		struct texture_batch* batch = &draw_res.texture_batches[i];
		glBindTexture(GL_TEXTURE_2D, batch->texture);
		glDrawElements(GL_TRIANGLES, batch->n_elements, ELEMENT_SIZE_GL, offset);
		offset += batch->n_elements;
	}

	draw_res.n_vertices = 0;
	draw_res.n_elements = 0;
	draw_res.n_texture_batches = 0;
}

static void draw_append(struct d_texture* texture, int n_vertices, int n_elements, struct draw_vertex* vertices, ElementType* elements)
{
	texture->draw_tag = draw_scope.tag;
	GLuint tid = texture->texture;

	int flush = 0;
	flush |= draw_res.n_vertices + n_vertices > MAX_VERTICES;
	flush |= draw_res.n_elements + n_elements > MAX_ELEMENTS;
	flush |= draw_res.n_texture_batches + 1 > MAX_TEXTURE_BINDS && draw_res.texture_batches[draw_res.n_texture_batches - 1].texture != tid;
	if (flush) {
		draw_flush();
		ASSERT((draw_res.n_vertices + n_vertices) <= MAX_VERTICES);
		ASSERT((draw_res.n_elements + n_elements) <= MAX_ELEMENTS);
		AZ(draw_res.n_texture_batches);
	}

	if (draw_res.n_texture_batches == 0 || draw_res.texture_batches[draw_res.n_texture_batches - 1].texture != tid) {
		struct texture_batch batch = {
			.n_elements = 0,
			.texture = tid
		};
		memcpy(draw_res.texture_batches + draw_res.n_texture_batches, &batch, sizeof(batch));
		draw_res.n_texture_batches++;
	}

	memcpy(draw_res.vertices + draw_res.n_vertices, vertices, n_vertices * sizeof(struct draw_vertex));

	ElementType* ebase = draw_res.elements + draw_res.n_elements;
	memcpy(ebase, elements, n_elements * sizeof(ElementType));
	for (int i = 0; i < n_elements; i++) ebase[i] += draw_res.n_vertices;

	draw_res.n_vertices += n_vertices;
	draw_res.n_elements += n_elements;
	draw_res.texture_batches[draw_res.n_texture_batches - 1].n_elements += n_elements;
}


//////////////////////////////////////////////
// public

void d_init()
{
	{
		if (gl3wInit() != 0) arghf("failed to initialize gl3w");
		if (!gl3wIsSupported(3, 0)) arghf("OpenGL 3.0 is not supported");
	}

	{
		const GLchar* vert_src =
			"#version 130\n"

			"uniform vec2 u_scaling;\n"

			"attribute vec2 a_position;\n"
			"attribute vec2 a_uv;\n"
			"attribute vec4 a_color;\n"

			"varying vec2 v_uv;\n"
			"varying vec4 v_color;\n"

			"void main()\n"
			"{\n"
			"	v_uv = a_uv;\n"
			"	v_color = a_color;\n"
			"	gl_Position = vec4(a_position * u_scaling * vec2(2,2) + vec2(-1,1), 0, 1);\n"
			"}\n"
			;

		const GLchar* frag_src =
			"#version 130\n"

			"uniform sampler2D u_texture;\n"

			"varying vec2 v_uv;\n"
			"varying vec4 v_color;\n"

			"void main()\n"
			"{\n"
			"	gl_FragColor = v_color * texture2D(u_texture, v_uv);\n"
			"}\n"
			;

		GLuint prg = draw_res.prg = create_program(vert_src, frag_src);

		draw_res.u_texture = glGetUniformLocation(prg, "u_texture"); CHKGL;
		draw_res.u_scaling = glGetUniformLocation(prg, "u_scaling"); CHKGL;

		GLuint a_position = glGetAttribLocation(prg, "a_position"); CHKGL;
		GLuint a_uv = glGetAttribLocation(prg, "a_uv"); CHKGL;
		GLuint a_color = glGetAttribLocation(prg, "a_color"); CHKGL;

		size_t vertices_sz = MAX_VERTICES * sizeof(struct draw_vertex);
		AN(draw_res.vertices = malloc(vertices_sz));
		glGenBuffers(1, &draw_res.vertex_buffer); CHKGL;
		glGenVertexArrays(1, &draw_res.vertex_array); CHKGL;
		glBindVertexArray(draw_res.vertex_array); CHKGL;
		glBindBuffer(GL_ARRAY_BUFFER, draw_res.vertex_buffer); CHKGL;
		glBufferData(GL_ARRAY_BUFFER, vertices_sz, NULL, GL_STREAM_DRAW); CHKGL;
		glEnableVertexAttribArray(a_position); CHKGL;
		glEnableVertexAttribArray(a_uv); CHKGL;
		glEnableVertexAttribArray(a_color); CHKGL;

		#define OFZ(e) (GLvoid*)((size_t)&(((struct draw_vertex*)0)->e))
		glVertexAttribPointer(a_position, 2, GL_FLOAT, GL_FALSE, sizeof(struct draw_vertex), OFZ(position)); CHKGL;
		glVertexAttribPointer(a_uv, 2, GL_FLOAT, GL_FALSE, sizeof(struct draw_vertex), OFZ(uv)); CHKGL;
		glVertexAttribPointer(a_color, 4, GL_FLOAT, GL_FALSE, sizeof(struct draw_vertex), OFZ(color)); CHKGL;
		#undef OFZ

		size_t elements_sz = MAX_ELEMENTS * sizeof(ElementType);
		AN(draw_res.elements = malloc(elements_sz));
		glGenBuffers(1, &draw_res.element_buffer); CHKGL;
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, draw_res.element_buffer); CHKGL;
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, elements_sz, NULL, GL_STREAM_DRAW); CHKGL;

		AN(draw_res.texture_batches = malloc(MAX_TEXTURE_BINDS * sizeof(struct texture_batch)));
	}
}

void d_inc_frame_tag()
{
	frame_tag++;
}

uint64_t d_get_frame_tag()
{
	return frame_tag;
}

void d_texture_init(struct d_texture* t, int width, int height)
{
	glGenTextures(1, &t->texture); CHKGL;

	int level = 0;
	int border = 0;
	glBindTexture(GL_TEXTURE_2D, t->texture);
	glTexImage2D(
		GL_TEXTURE_2D,
		level,
		GL_RGBA,
		width, height,
		border,
		GL_RGBA,
		GL_UNSIGNED_BYTE,
		NULL); CHKGL;

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); CHKGL;
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR); CHKGL;
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); CHKGL;
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE); CHKGL;

	t->width = width;
	t->height = height;

	t->draw_tag = 0;
}

void d_texture_free(struct d_texture* t)
{
	glDeleteTextures(1, &t->texture);
	memset(t, 0, sizeof(*t));
}

void d_texture_clear(struct d_texture* t)
{
	MTS_ENTER(0);
	void* tmp = MTS_calloc_ptr(t->width * t->height * 4);
	d_texture_sub_image(t, 0, 0, t->width, t->height, tmp);
	MTS_LEAVE(0);
}

void d_texture_sub_image(struct d_texture* t, int x, int y, int w, int h, void* data)
{
	if (t->draw_tag == draw_scope.tag) {
		/* texture is being used in current draw scope, so flush
		 * pending draw commands before altering the texture */
		draw_flush();
		t->draw_tag = 0;
	}

	int level = 0;
	glBindTexture(GL_TEXTURE_2D, t->texture);

	glTexSubImage2D(
		GL_TEXTURE_2D,
		level,
		x, y,
		w, h,
		GL_RGBA,
		GL_UNSIGNED_BYTE,
		data); CHKGL;
}

void d_texture_sub_image_intensity(struct d_texture* t, int x, int y, int w, int h, void* restrict data)
{
	MTS_ENTER(0);
	char* restrict tmp = MTS_alloc_ptr(w * h * 4);
	int inp = 0;
	int outp = 0;
	int n = w * h;
	for (int i = 0; i < n; i++) {
		char in = ((char*)data)[inp++];
		for (int j = 0; j < 4; j++) tmp[outp++] = in;
	}
	d_texture_sub_image(t, x, y, w, h, tmp);
	MTS_LEAVE(0);
}

void d_rect(float x, float y, float width, float height)
{
	float x0 = x;
	float y0 = y;
	float x1 = x + width;
	float y1 = y + height;
	float u,v;
	d_main_atlas_get_dot_uv(&u, &v);
	struct draw_vertex vs[4] = {
		{ .position = { .x = x0, .y = y0 }, .uv = { .u = u, .v = v }, .color = draw_scope.color0 },
		{ .position = { .x = x1, .y = y0 }, .uv = { .u = u, .v = v }, .color = draw_scope.color0 },
		{ .position = { .x = x1, .y = y1 }, .uv = { .u = u, .v = v }, .color = draw_scope.color1 },
		{ .position = { .x = x0, .y = y1 }, .uv = { .u = u, .v = v }, .color = draw_scope.color1 }
	};
	ElementType es[6] = {0,1,2,0,2,3};
	draw_append(d_main_atlas_get_texture(), 4, 6, vs, es);
}

void d_blit(struct d_texture* t, int sx, int sy, int sw, int sh, float dx, float dy)
{
	float dx0 = dx;
	float dy0 = dy;
	float dx1 = dx + sw;
	float dy1 = dy + sh;

	float u0,v0,u1,v1;
	d_texture_get_uv(t, sx, sy, &u0, &v0);
	d_texture_get_uv(t, sx + sw, sy + sh, &u1, &v1);

	struct draw_vertex vs[4] = {
		{ .position = { .x = dx0, .y = dy0 }, .uv = { .u = u0, .v = v0 }, .color = draw_scope.color0 },
		{ .position = { .x = dx1, .y = dy0 }, .uv = { .u = u1, .v = v0 }, .color = draw_scope.color0 },
		{ .position = { .x = dx1, .y = dy1 }, .uv = { .u = u1, .v = v1 }, .color = draw_scope.color1 },
		{ .position = { .x = dx0, .y = dy1 }, .uv = { .u = u0, .v = v1 }, .color = draw_scope.color1 }
	};
	ElementType es[6] = {0,1,2,0,2,3};
	draw_append(t, 4, 6, vs, es);
}

void d_begin(int win_id)
{
	AZ(draw_scope.begun);
	draw_scope.begun = 1;
	draw_scope.tag++;

	win_make_current(win_id);

	draw_scope.win_id = win_id;
	win_get_size(win_id, &draw_scope.win_width, &draw_scope.win_height);

	glViewport(0, 0, draw_scope.win_width, draw_scope.win_height); CHKGL;

	// premultiplied alpha
	glEnable(GL_BLEND);
	glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA); CHKGL;

	// XXX leave out?
	glClearColor(0, 0, 0, 0);
	glClear(GL_COLOR_BUFFER_BIT);

	glUseProgram(draw_res.prg);
	glUniform1i(draw_res.u_texture, 0);
	glUniform2f(draw_res.u_scaling, 1.0f / (float)draw_scope.win_width, -1.0f / (float)draw_scope.win_height);

	glBindVertexArray(draw_res.vertex_array);
}

void d_end()
{
	AN(draw_scope.begun);
	draw_scope.begun = 0;
	draw_flush();
}

void d_set_color(union vec4 color)
{
	draw_scope.color0 = draw_scope.color1 = color;
}

void d_set_vertical_shade(union vec4 color0, union vec4 color1)
{
	draw_scope.color0 = color0;
	draw_scope.color1 = color1;
}
