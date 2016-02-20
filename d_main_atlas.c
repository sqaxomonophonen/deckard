#include "a.h"
#include "d.h"
#include "scratch.h"

#define STBRP_ASSERT ASSERT
#define STBRP_STATIC
#define STB_RECT_PACK_IMPLEMENTATION
#include "stb_rect_pack.h"

static int initialized;
static struct d_texture texture;
static stbrp_context rp_ctx;
static stbrp_node* rp_nodes;
float dot_u, dot_v;

static int rect_pack(short width, short height, short* x, short* y)
{
	stbrp_rect rect;
	rect.w = width + 2;
	rect.h = height + 2;
	stbrp_pack_rects(&rp_ctx, &rect, 1);
	*x = rect.x + 1;
	*y = rect.y + 1;
	return rect.was_packed ? 0 : -1;
}

static void pack_dot()
{
	char dot[] = {0xff, 0xff, 0xff, 0xff};
	short x, y;
	d_main_atlas_pack_intensity(2, 2, dot, &x, &y);
	d_texture_get_uv(d_main_atlas_get_texture(), x+1, y+1, &dot_u, &dot_v);
}

static inline void initialize()
{
	if (!initialized) d_main_atlas_reset();
	AN(initialized);
}

struct d_texture* d_main_atlas_get_texture()
{
	initialize();
	return &texture;
}

void d_main_atlas_reset()
{
	int width = 1 << 11;
	int height = width;
	int n_nodes = width;

	if (!initialized) {
		AN(rp_nodes = calloc(n_nodes, sizeof(stbrp_node)));
		initialized = 1;
		d_texture_init(&texture, width, height);
	}

	stbrp_init_target(
		&rp_ctx,
		width, height,
		rp_nodes, n_nodes);
	stbrp_setup_heuristic(
		&rp_ctx,
		STBRP_HEURISTIC_Skyline_default);

	d_texture_clear(&texture);

	pack_dot();
}

int d_main_atlas_pack(short width, short height, void* data, short* x, short* y)
{
	int r = rect_pack(width, height, x, y);
	if (r == 0) d_texture_sub_image(&texture, *x, *y, width, height, data);
	return r;
}

int d_main_atlas_pack_intensity(short width, short height, void* data, short* x, short* y)
{
	int r = rect_pack(width, height, x, y);
	if (r == 0) d_texture_sub_image_intensity(&texture, *x, *y, width, height, data);
	return r;
}

void d_main_atlas_get_dot_uv(float* u, float* v)
{
	initialize();
	if (u != NULL) *u = dot_u;
	if (v != NULL) *v = dot_v;
}

