#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include "a.h"
#include "d.h"
#include "sys.h"
#include "utf8_decode.h"

#define STBRP_ASSERT ASSERT
#define STBRP_STATIC
#define STB_RECT_PACK_IMPLEMENTATION
#include "stb_rect_pack.h"

#if 0
#define STBTT_assert ASSERT
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#endif

#define MAX_FONT_HANDLES (256)
#define MAX_GLYPH_SIZE (256)

static char* builtins =
	"Aileron Regular\0" "builtin:Aileron-Regular.otf\0"
	"\0"; // end of list

struct font {
	int open;
	struct sys_mmap_file filemmap;
	int size;
	int has_kerning;
	FT_Face face;
};

static struct font fonts[MAX_FONT_HANDLES];

struct glyph_cache_entry_key {
	int codepoint;
	short font_handle;
};

struct glyph_cache_entry_info {
	short x,y;
	short w,h;
	short top, left;
	float advance_x;
	int glyph_index;
};

static inline int glyph_cache_key_compar(struct glyph_cache_entry_key a, struct glyph_cache_entry_key b)
{
	int c0 = a.codepoint - b.codepoint;
	if (c0 != 0) return c0;

	return a.font_handle - b.font_handle;
}

struct glyph_cache {
	int initialized;
	int width, height, prev_width, prev_height;

	struct d_texture texture;

	// ft2
	int ft2_init;
	FT_Library ft2;

	// stb_rectpack.h stuff
	stbrp_context rp_ctx;
	stbrp_node* rp_nodes;

	// glyph entries
	int max_entries, n_entries;
	struct glyph_cache_entry_key* entry_keys;
	struct glyph_cache_entry_info* entry_info;
	uint64_t* entry_tags;
	int* entry_qsort_indices; // qsort helper
};

static struct {
	float x0, x, y;
	struct glyph_cache glyph_cache;
} state;


static int find_free_font_handle()
{
	for (int i = 0; i < MAX_FONT_HANDLES; i++) if (!fonts[i].open) return i;
	return -1;
}

static int open_font(char* path, int index, int size)
{
	int font_handle = find_free_font_handle();
	if (font_handle == -1) {
		// no free font handle
		return -1;
	}

	struct font* f = &fonts[font_handle];

	if (sys_mmap_file_ro(&f->filemmap, path) == -1) {
		return -1;
	}

	int err = FT_New_Memory_Face(
		state.glyph_cache.ft2,
		f->filemmap.ptr,
		f->filemmap.sz,
		index,
		&f->face);
	if (err) {
		sys_munmap_file(&f->filemmap);
		return -1;
	}

	err = FT_Set_Pixel_Sizes(f->face, 0, size);
	if (err) {
		FT_Done_Face(f->face);
		sys_munmap_file(&f->filemmap);
		return -1;
	}

	f->size = size;
	f->has_kerning = FT_HAS_KERNING(f->face);
	f->open = 1;

	return font_handle;
}

static int get_stbrp_node_count()
{
	return state.glyph_cache.width;
}

static void reset_rect_pack()
{
	struct glyph_cache* gc = &state.glyph_cache;
	stbrp_init_target(
		&gc->rp_ctx,
		gc->width,
		gc->height,
		gc->rp_nodes,
		get_stbrp_node_count());
	stbrp_setup_heuristic(
		&gc->rp_ctx,
		STBRP_HEURISTIC_Skyline_default);
}

static void reset_glyph_cache()
{
	struct glyph_cache* gc = &state.glyph_cache;

	if (!gc->initialized) {
		AZ(FT_Init_FreeType(&gc->ft2) != 0);
	}

	int new_dimensions = gc->width != gc->prev_width || gc->height != gc->prev_height;

	if (new_dimensions) {
		if (gc->rp_nodes) free(gc->rp_nodes);
		AN(gc->rp_nodes = calloc(get_stbrp_node_count(), sizeof(stbrp_node)));
	}

	reset_rect_pack();

	if (new_dimensions) {
		if (gc->initialized) d_texture_free(&gc->texture);
		d_texture_init(&gc->texture, 1, gc->width, gc->height);

		if (gc->entry_keys != NULL) free(gc->entry_keys);
		if (gc->entry_info != NULL) free(gc->entry_info);
		if (gc->entry_tags != NULL) free(gc->entry_tags);
		if (gc->entry_qsort_indices != NULL) free(gc->entry_qsort_indices);

		gc->max_entries = (gc->width*gc->height) / (4*4);
		AN(gc->entry_keys = calloc(gc->max_entries, sizeof(*gc->entry_keys)));
		AN(gc->entry_info = calloc(gc->max_entries, sizeof(*gc->entry_info)));
		AN(gc->entry_tags = calloc(gc->max_entries, sizeof(*gc->entry_tags)));
		AN(gc->entry_qsort_indices = calloc(gc->max_entries, sizeof(*gc->entry_qsort_indices)));
	}

	gc->n_entries = 0;

	gc->initialized = 1;
	gc->prev_width = gc->width;
	gc->prev_height = gc->height;
}

static int _repack_tag_compar(const void* va, const void* vb)
{
	const int ia = *((int*)va);
	const int ib = *((int*)vb);
	struct glyph_cache* gc = &state.glyph_cache;
	return gc->entry_tags[ib] - gc->entry_tags[ia];
}

static int _repack_key_compar(const void* va, const void* vb)
{
	const int ia = *((int*)va);
	const int ib = *((int*)vb);
	struct glyph_cache* gc = &state.glyph_cache;
	return glyph_cache_key_compar(gc->entry_keys[ia], gc->entry_keys[ib]);
}

static int pack_glyph(int font_handle, int glyph_index, stbrp_rect* rect)
{
	AN(rect);

	struct font* font = &fonts[font_handle];
	FT_Face face = font->face;

	if (FT_Load_Glyph(face, glyph_index, 0) != 0) {
		return -1;
	}

	if (FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL) != 0) {
		return -1;
	}

	ASSERT(face->glyph->bitmap.width == face->glyph->bitmap.pitch);
	int glyph_width = face->glyph->bitmap.width;
	int glyph_height = face->glyph->bitmap.rows;
	ASSERT(glyph_width >= 0);
	ASSERT(glyph_height >= 0);

	if (glyph_width > MAX_GLYPH_SIZE || glyph_height > MAX_GLYPH_SIZE) {
		// do not honor huge glyphs
		return -1;
	}

	rect->w = glyph_width + 2;
	rect->h = glyph_height + 2;
	stbrp_pack_rects(&state.glyph_cache.rp_ctx, rect, 1);
	if (!rect->was_packed) {
		return -2;
	}

	d_texture_sub_image(
		&state.glyph_cache.texture,
		rect->x + 1, rect->y + 1,
		glyph_width, glyph_height,
		face->glyph->bitmap.buffer);

	return 0;
}

static int repack_glyph_cache()
{
	struct glyph_cache* gc = &state.glyph_cache;

	// initialize indicies for qsort
	for (int i = 0; i < gc->n_entries; i++) {
		gc->entry_qsort_indices[i] = i;
	}

	// sort indices by MRU (Most Recently Used)
	qsort(gc->entry_qsort_indices, gc->n_entries, sizeof(*gc->entry_qsort_indices), _repack_tag_compar);

	// keep 20% MRU
	gc->n_entries /= 5;

	// sort indices by key order
	qsort(gc->entry_qsort_indices, gc->n_entries, sizeof(*gc->entry_qsort_indices), _repack_key_compar);

	// move glyph cache entries into new positions
	for (int i = 0; i < gc->n_entries; i++) {
		int j = gc->entry_qsort_indices[i];
		PARANOID_ASSERT(i <= j);
		PARANOID_ASSERT(i == 0 || gc->entry_qsort_indices[i-1] < j);
		if (i == j) continue;
		memcpy(&gc->entry_keys[i], &gc->entry_keys[j], sizeof(*gc->entry_keys));
		memcpy(&gc->entry_info[i], &gc->entry_info[j], sizeof(*gc->entry_info));
		memcpy(&gc->entry_tags[i], &gc->entry_info[j], sizeof(*gc->entry_tags));
	}

	// repack glyphs
	reset_rect_pack();
	d_texture_clear(&gc->texture);
	for (int i = 0; i < gc->n_entries; i++) {
		struct glyph_cache_entry_info* info = &gc->entry_info[i];
		stbrp_rect rect;
		int ret = pack_glyph(
			gc->entry_keys[i].font_handle,
			info->glyph_index,
			&rect);
		if (ret < 0) {
			PARANOID_ASSERT(ret != -1); // we've packed it before!
			return -2;
		}
		info->x = rect.x;
		info->y = rect.y;
	}

	return 0;
}

static int _find_or_insert_glyph_cache_entry_index(struct glyph_cache_entry_key key)
{
	ASSERT(key.font_handle >= 0);
	ASSERT(key.font_handle < MAX_FONT_HANDLES);

	struct glyph_cache* gc = &state.glyph_cache;
	struct glyph_cache_entry_key* keys = gc->entry_keys;

	int i = 0;
	int cmp = -1;

	if (gc->n_entries > 0) {
		// binary search for codepoint match
		int imin = 0;
		int imax = gc->n_entries - 1;
		while (imin < imax) {
			int imid = (imin + imax) >> 1;
			PARANOID_ASSERT(imid < imax);
			if (keys[imid].codepoint < key.codepoint) {
				imin = imid + 1;
			} else {
				imax = imid;
			}
		}

		i = imin;

		PARANOID_ASSERT(i >= 0);
		PARANOID_ASSERT(i < gc->n_entries);

		cmp = glyph_cache_key_compar(keys[i], key);

		if (imin == imax && keys[imin].codepoint == key.codepoint) {
			// codepoint found; linear search for exact match
			if (cmp == 0) {
				return i;
			} else if (cmp < 0) {
				for (;;) {
					if (i == 0) break;
					i--;
					PARANOID_ASSERT(i >= 0);
					cmp = glyph_cache_key_compar(keys[i], key);
					if (cmp == 0) {
						return i;
					} else if (cmp > 0) {
						break;
					}
				}
			} else if (cmp > 0) {
				for (;;) {
					if (i == (gc->n_entries - 1)) break;
					i++;
					PARANOID_ASSERT(i < gc->n_entries);
					cmp = glyph_cache_key_compar(keys[i], key);
					if (cmp == 0) {
						return i;
					} else if (cmp < 0) {
						break;
					}
				}
			}
		}
	}

	// no exact match found; insert entry
	if (gc->n_entries >= gc->max_entries) {
		return -2;
	}

	struct font* font = &fonts[key.font_handle];
	ASSERT(font->open);

	int glyph_index = FT_Get_Char_Index(font->face, key.codepoint);
	if (glyph_index == 0) {
		// font doesn't have this codepoint
		return -1;
	}

	stbrp_rect rect;
	pack_glyph(key.font_handle, glyph_index, &rect);

	int insert_before = cmp > 0 ? i : i + 1;
	PARANOID_ASSERT(insert_before >= 0);

	int nmm = gc->n_entries - insert_before;
	if (nmm > 0) {
		// move entries forward to make room
		memmove(&gc->entry_keys[insert_before + 1], &gc->entry_keys[insert_before], nmm * sizeof(*gc->entry_keys));
		memmove(&gc->entry_info[insert_before + 1], &gc->entry_info[insert_before], nmm * sizeof(*gc->entry_info));
		memmove(&gc->entry_tags[insert_before + 1], &gc->entry_tags[insert_before], nmm * sizeof(*gc->entry_tags));
	}

	gc->entry_keys[insert_before] = key;
	gc->entry_info[insert_before] = (struct glyph_cache_entry_info) {
		.x = rect.x,
		.y = rect.y,
		.w = rect.w,
		.h = rect.h,
		.top = font->face->glyph->bitmap_top,
		.left = font->face->glyph->bitmap_left,
		.advance_x = (float)font->face->glyph->advance.x / 64.0,
		.glyph_index = glyph_index
	};

	gc->n_entries++;

	return insert_before;
}

static struct glyph_cache_entry_info* find_or_insert_glyph_cache_entry_info(struct glyph_cache_entry_key key)
{
	int i = _find_or_insert_glyph_cache_entry_index(key);
	if (i == -1) {
		return NULL;
	} else if (i == -2) {
		if (repack_glyph_cache() == -1) {
			reset_glyph_cache();
			i = _find_or_insert_glyph_cache_entry_index(key);
			if (i < 0) {
				PARANOID_WRONG("repack fail -> reset; still no room!");
				return NULL;
			}
		} else {
			i = _find_or_insert_glyph_cache_entry_index(key);
			if (i == -1) {
				return NULL;
			} else if (i == -2) {
				reset_glyph_cache();
				i = _find_or_insert_glyph_cache_entry_index(key);
				if (i < 0) {
					PARANOID_WRONG("repack success -> reset; still no room!");
					return NULL;
				}
			}
		}
	}

	state.glyph_cache.entry_tags[i] = d_get_frame_tag();
	return &state.glyph_cache.entry_info[i];
}

static float get_kerning(int font_handle, int prev, int cur)
{
	FT_Face face = fonts[font_handle].face;
	FT_Vector delta;
	if (FT_Get_Kerning(face, prev, cur, FT_KERNING_DEFAULT, &delta) != 0) return 0;
	return (float)delta.x / 64.0;
}

static int draw_string_n(int font_handle, int n, char* str)
{
	char* p = str;
	int prev_glyph_index = 0;
	while (*p) {
		int codepoint = utf8_decode(&p, &n);
		AN(codepoint); // encountering NUL implies programming error
		if (codepoint == -1) {
			// invalid utf8 encoding
			return -1;
		}

		if (codepoint == '\n') {
			state.x = state.x0;
			// TODO state.y += ???
			prev_glyph_index = 0;
			continue;
		}

		struct glyph_cache_entry_key key = {
			.codepoint = codepoint,
			.font_handle = font_handle
		};
		struct glyph_cache_entry_info* info = find_or_insert_glyph_cache_entry_info(key);
		if (info == NULL) {
			key.codepoint = 0xfffd; // replacement character
			info = find_or_insert_glyph_cache_entry_info(key);
			if (info == NULL) {
				prev_glyph_index = 0;
				continue;
			}
		}

		if (fonts[font_handle].has_kerning && prev_glyph_index && info->glyph_index) {
			state.x += get_kerning(font_handle, prev_glyph_index, info->glyph_index);
		}

		d_blit(
			&state.glyph_cache.texture,
			info->x, info->y, info->w, info->h,
			state.x + info->left, state.y - info->top);

		state.x += info->advance_x;

		prev_glyph_index = info->glyph_index;
	}
	return 0;
}



////////////////////////////////////////
/// public

void d_reset_glyph_cache(int width, int height)
{
	state.glyph_cache.width = width;
	state.glyph_cache.height = height;
	reset_glyph_cache();
}

char* d_font_get_list()
{
	/* TODO merge with fontconfig stuff? and pass pointer to internally
	 * malloc'd structure probably. that's what d.h says; pointer is owned
	 * by d_font.c */
	return builtins;
}

int d_open_font(char* font_spec, int size)
{
	char* colon_pos = strchr(font_spec, ':');
	if (colon_pos == NULL) {
		// invalid font_spec; no colon
		return -1;
	}

	if (memcmp("builtin", font_spec, colon_pos - font_spec) == 0) {
		char* builtin_font_name = colon_pos + 1;

		char* p = builtins;
		while (*p != 0) {
			// skip display name
			while (*p != 0) p++;
			p++;

			ASSERT(memcmp("builtin:", p, 8) == 0);
			p += 8;

			if (strcmp(builtin_font_name, p) == 0) {
				return open_font(p, 0, size);
			}

			while (*p != 0) p++;
			p++;
		}

		// builtin not found
		return -1;
	} else {
		// invalid font_spec; invalid prefix
		return -1;
	}
}

/* close font; pass font_handle returned by d_open_font */
void d_close_font(int font_handle)
{
	// XXX TODO repack glyph cache? not only for performance; things might
	// explode in hillarious ways if a font_handle is reused

	ASSERT(font_handle >= 0);
	ASSERT(font_handle < MAX_FONT_HANDLES);
	struct font* f = &fonts[font_handle];
	AN(f->open);
	FT_Done_Face(f->face);
	sys_munmap_file(&f->filemmap);
	f->open = 0;
}

void d_text_set_cursor(float x, float y)
{
	state.x0 = state.x = x;
	state.y = y;
}

int d_str(int font_handle, char* str)
{
	return draw_string_n(font_handle, strlen(str), str);
}

int d_printf(int font_handle, const char* fmt, ...)
{
	char buffer[4096];
	va_list args;
	va_start(args, fmt);
	int ret = vsnprintf(buffer, sizeof(buffer), fmt, args);
	va_end(args);

	if (ret < 0) {
		return -1;
	}

	int n = ret >= sizeof(buffer) ? sizeof(buffer)-1 : ret;
	if (draw_string_n(font_handle, n, buffer) == -1) {
		return -1;
	}

	return ret;
}

