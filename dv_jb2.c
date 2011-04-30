#include "mudjvu.h"

enum {
	BIGPOS = 262142,
	BIGNEG = -262143,
	CHUNK = 20000,
};

enum {
	START_OF_DATA = 0,
	NEW_SYMBOL = 1,
	NEW_SYMBOL_LIBRARY_ONLY = 2,
	NEW_SYMBOL_IMAGE_ONLY = 3,
	MATCHED_REFINE = 4,
	MATCHED_REFINE_LIBRARY_ONLY = 5,
	MATCHED_REFINE_IMAGE_ONLY = 6,
	MATCHED_COPY = 7,
	NON_SYMBOL_DATA = 8,
	REQUIRED_DICT_OR_RESET = 9,
	PRESERVED_COMMENT = 10,
	END_OF_DATA = 11
};

struct bitmap {
	int w, h;
	int stride;
	unsigned char *data;
	unsigned char *orig;
};

struct jb2_decoder {
	struct zp_decoder zp;

	/* symbol library */
	int symlen, symcap;
	struct bitmap **symbol;

	/* relative locations */
	int line_left, line_bottom;
	int prev_right, prev_bottom;
	int baseline[3], baseline_pos;

	struct bitmap *page;

	/* number coder */
	int cur, len;
	unsigned char *bit;
	unsigned int *left;
	unsigned int *right;

	/* number contexts */
	unsigned int dist_comment_byte;
	unsigned int dist_comment_length;
	unsigned int dist_record_type;
	unsigned int dist_match_index;
	unsigned int abs_loc_x;
	unsigned int abs_loc_y;
	unsigned int abs_size_x;
	unsigned int abs_size_y;
	unsigned int image_size_dist;
	unsigned int inherited_shape_count_dist;
	unsigned int rel_loc_x_prev;
	unsigned int rel_loc_x_first;
	unsigned int rel_loc_y_prev;
	unsigned int rel_loc_y_first;
	unsigned int rel_size_x;
	unsigned int rel_size_y;

	/* bit contexts */
	unsigned char dist_refinement_flag;
	unsigned char offset_type_dist;

	/* bitmap contexts */
	unsigned char direct[1024];
	unsigned char refine[2048];
};

static void
jb2_grow_num_coder(struct jb2_decoder *jb)
{
	int newlen = jb->len + CHUNK;
	jb->bit = realloc(jb->bit, newlen);
	jb->left = realloc(jb->left, newlen * sizeof(unsigned int));
	jb->right = realloc(jb->right, newlen * sizeof(unsigned int));
	memset(jb->bit + jb->len, 0, CHUNK);
	memset(jb->left + jb->len, 0, CHUNK * sizeof(unsigned int));
	memset(jb->right + jb->len, 0, CHUNK * sizeof(unsigned int));
	jb->len = newlen;
}

static void
jb2_reset_num_coder(struct jb2_decoder *jb)
{
	jb->dist_comment_byte = 0;
	jb->dist_comment_length = 0;
	jb->dist_record_type = 0;
	jb->dist_match_index = 0;
	jb->abs_loc_x = 0;
	jb->abs_loc_y = 0;
	jb->abs_size_x = 0;
	jb->abs_size_y = 0;
	jb->image_size_dist = 0;
	jb->inherited_shape_count_dist = 0;
	jb->rel_loc_x_prev = 0;
	jb->rel_loc_x_first = 0;
	jb->rel_loc_y_prev = 0;
	jb->rel_loc_y_first = 0;
	jb->rel_size_x = 0;
	jb->rel_size_y = 0;

	memset(jb->bit, 0, jb->len);
	memset(jb->left, 0, jb->len * sizeof(unsigned int));
	memset(jb->right, 0, jb->len * sizeof(unsigned int));
	jb->cur = 1;
}

static int
jb2_decode_num(struct jb2_decoder *jb, int low, int high, unsigned int *ctx)
{
	int negative, cutoff, phase, range, decision;

	negative = 0;
	cutoff = 0;
	phase = 1;
	range = 0xffffffff;

	while (range != 1) {
		if (!*ctx) {
			if (jb->cur >= jb->len)
				jb2_grow_num_coder(jb);
			*ctx = jb->cur++;
			jb->bit[*ctx] = 0;
			jb->left[*ctx] = 0;
			jb->right[*ctx] = 0;
		}

		decision = low >= cutoff ||
			(high >= cutoff && zp_decode(&jb->zp, jb->bit + *ctx));

		ctx = decision ? jb->right + *ctx : jb->left + *ctx;

		switch (phase) {
		case 1:
			negative = !decision;
			if (negative) {
				int tmp = -low - 1;
				low = -high - 1;
				high = tmp;
			}
			phase = 2;
			cutoff = 1;
			break;

		case 2:
			if (!decision) {
				phase = 3;
				range = (cutoff + 1) / 2;
				if (range == 1)
					cutoff = 0;
				else
					cutoff -= range / 2;
			} else {
				cutoff += cutoff + 1;
			}
			break;

		case 3:
			range = range / 2;
			if (range != 1) {
				if (!decision)
					cutoff -= range / 2;
				else
					cutoff += range / 2;
			} else if (!decision) {
				cutoff--;
			}
			break;
		}
	}

	printf("jb2 num (%d,%d) = %d\n", low, high, (negative)?(- cutoff - 1):cutoff);
	return negative ? -cutoff - 1 : cutoff;
}

static struct bitmap *
jb2_new_bitmap(int w, int h)
{
	struct bitmap *bm = malloc(sizeof(struct bitmap));
	bm->w = w;
	bm->h = h;
	bm->stride = w;
	bm->data = malloc(w * h);
	bm->orig = bm->data;
	memset(bm->data, 0, w * h);
	return bm;
}

static inline int getbit(struct bitmap *bm, int x, int y)
{
	if (x < 0 || y < 0 || x >= bm->w || y >= bm->h)
		return 0;
	return bm->data[y * bm->stride + x];
}

static inline void setbit(struct bitmap *bm, int x, int y, int v)
{
	if (x < 0 || y < 0 || x >= bm->w || y >= bm->h)
		return;
	bm->data[y * bm->stride + x] = v;
}

static void
jb2_free_bitmap(struct bitmap *bm)
{
	free(bm->orig);
	free(bm);
}

static void
jb2_blit_bitmap(struct bitmap *dst, struct bitmap *src, int dx, int dy)
{
	int x, y;
	for (y = 0; y < src->h; y++) {
		for (x = 0; x < src->w; x++) {
			if (getbit(src, x, y))
				setbit(dst, dx+x, dy+y, 0);
		}
	}
}

static int
jb2_get_bbox_x0(struct bitmap *bm)
{
	int x, y;
	for (x = 0; x < bm->w; x++)
		for (y = 0; y < bm->h; y++)
			if (getbit(bm, x, y))
				return x;
	return 0;
}

static int
jb2_get_bbox_x1(struct bitmap *bm)
{
	int x, y;
	for (x = bm->w - 1; x >= 0; x--)
		for (y = 0; y < bm->h; y++)
			if (getbit(bm, x, y))
				return x;
	return 0;
}

static int
jb2_get_bbox_y0(struct bitmap *bm)
{
	int x, y;
	for (y = 0; y < bm->h; y++)
		for (x = 0; x < bm->w; x++)
			if (getbit(bm, x, y))
				return y;
	return 0;
}

static int
jb2_get_bbox_y1(struct bitmap *bm)
{
	int x, y;
	for (y = bm->h - 1; y >= 0; y--)
		for (x = 0; x < bm->w; x++)
			if (getbit(bm, x, y))
				return y;
	return 0;
}

static void
jb2_trim_edges(struct bitmap *bm)
{
	int x0 = jb2_get_bbox_x0(bm);
	int y0 = jb2_get_bbox_y0(bm);
	int x1 = jb2_get_bbox_x1(bm);
	int y1 = jb2_get_bbox_y1(bm);
	bm->w = x1 - x0 + 1;
	bm->h = y1 - y0 + 1;
	bm->data += x0 + y0 * bm->stride;
}

static void
jb2_print_bitmap(struct bitmap *bm)
{
	int x, y;
//	printf("jb2: bitmap bbox=%d,%d,%d,%d\n", bm->x0, bm->y0, bm->x1, bm->y1);
	for (y = 0; y < bm->h; y++) {
		printf("jb2: ");
		for (x = 0; x < bm->w; x++)
			putchar(getbit(bm, x, y) ? '#' : '-');
		putchar('\n');
	}
//	printf("jb2: end bitmap\n");
}

static int
jb2_direct_context(struct bitmap *bm, int x, int y)
{
	return	getbit(bm, x-1, y-2) << 9 |
		getbit(bm, x, y-2) << 8 |
		getbit(bm, x+1, y-2) << 7 |
		getbit(bm, x-2, y-1) << 6 |
		getbit(bm, x-1, y-1) << 5 |
		getbit(bm, x, y-1) << 4 |
		getbit(bm, x+1, y-1) << 3 |
		getbit(bm, x+2, y-1) << 2 |
		getbit(bm, x-2, y) << 1 |
		getbit(bm, x-1, y);
}

static int
jb2_shift_direct_context(struct bitmap *bm, int x, int y, int ctx, int v)
{
	return ((ctx << 1) & 0x37a) |
		getbit(bm, x+2, y-1) << 2 |
		getbit(bm, x+1, y-2) << 7 |
		v;
}

static struct bitmap *
jb2_decode_bitmap_direct(struct jb2_decoder *jb, int w, int h)
{
	struct bitmap *bm;
	int ctx, x, y, v;

	bm = jb2_new_bitmap(w, h);

	for (y = 0; y < h; y++) {
		ctx = jb2_direct_context(bm, 0, y);
		for (x = 0; x < w; x++) {
			v = zp_decode(&jb->zp, jb->direct + ctx);
			setbit(bm, x, y, v);
			ctx = jb2_shift_direct_context(bm, x+1, y, ctx, v);
		}
	}

//	jb2_print_bitmap(bm);

	return bm;
}

static int
jb2_refine_context(struct bitmap *dm, int dx, int dy,
		struct bitmap *sm, int sx, int sy)
{
	return	getbit(dm, dx-1, dy-1) << 10 |
		getbit(dm, dx, dy-1) << 9 |
		getbit(dm, dx+1, dy-1) << 8 |
		getbit(dm, dx-1, dy) << 7 |
		getbit(sm, sx, sy-1) << 6 |
		getbit(sm, sx-1, sy) << 5 |
		getbit(sm, sx, sy) << 4 |
		getbit(sm, sx+1, sy) << 3 |
		getbit(sm, sx-1, sy+1) << 2 |
		getbit(sm, sx, sy+1) << 1 |
		getbit(sm, sx+1, sy+1);
}

static int
jb2_shift_refine_context(struct bitmap *dm, int dx, int dy,
		struct bitmap *sm, int sx, int sy,
		int ctx, int v)
{
	return ((ctx << 1) & 0x636) |
		getbit(dm, dx+1, dy-1) << 8 |
		getbit(sm, sx, sy-1) << 6 |
		getbit(sm, sx+1, sy) << 3 |
		getbit(sm, sx+1, sy+1) |
		v << 7;
}

static struct bitmap *
jb2_decode_bitmap_refine(struct jb2_decoder *jb, int s, int xdiff, int ydiff)
{
	struct bitmap *bm, *sm = jb->symbol[s];
	int w = sm->w + xdiff;
	int h = sm->h + ydiff;
	int xoff = (sm->w-1) / 2 - (w-1) / 2;
	int yoff = sm->h / 2 - h / 2;
	int x, y, ctx, v;

	bm = jb2_new_bitmap(w, h);

	for (y = 0; y < h; y++) {
		ctx = jb2_refine_context(bm, 0, y, sm, xoff, y+yoff);
//printf("jb2: (%04x) ", ctx);
		for (x = 0; x < w; x++) {
			ctx = jb2_refine_context(bm, x, y, sm, x+xoff, y+yoff);
			v = zp_decode(&jb->zp, jb->refine + ctx);
			setbit(bm, x, y, v);
//if (getbit(sm, x+xoff, y+yoff) != v) printf("%c", v?'X':'.'); else printf("%c", v?'#':'-');
			ctx = jb2_shift_refine_context(bm, x+1, y, sm, x+xoff+1, y+yoff, ctx, v);
		}
//printf("\n");
	}

	return bm;
}

static void
jb2_add_to_library(struct jb2_decoder *jb, struct bitmap *bm)
{
	if (jb->symlen + 1 >= jb->symcap) {
		jb->symcap += 1000;
		jb->symbol = realloc(jb->symbol, jb->symcap * sizeof(struct bitmap*));
	}
	jb2_trim_edges(bm);
	jb->symbol[jb->symlen++] = bm;
}

static void
jb2_set_baseline(struct jb2_decoder *jb, int y)
{
	jb->baseline[0] = y;
	jb->baseline[1] = y;
	jb->baseline[2] = y;
	jb->baseline_pos = 0;
}

static int
jb2_update_baseline(struct jb2_decoder *jb, int y)
{
	int *s = jb->baseline;
	if (++jb->baseline_pos == 3)
		jb->baseline_pos = 0;
	s[jb->baseline_pos] = y;

	return s[0] >= s[1] ?
		(s[0] > s[2] ? (s[1] >= s[2] ? s[1] : s[2]) : s[0]) :
		(s[0] < s[2] ? (s[1] >= s[2] ? s[2] : s[1]) : s[0]);
}

static void
jb2_decode_rel_loc(struct jb2_decoder *jb, struct bitmap *bm, int *xp, int *yp)
{
	int dx, dy, left, right, top, bottom, t;
	t = zp_decode(&jb->zp, &jb->offset_type_dist);
	if (t) {
		dx = jb2_decode_num(jb, BIGNEG, BIGPOS, &jb->rel_loc_x_first);
		dy = -jb2_decode_num(jb, BIGNEG, BIGPOS, &jb->rel_loc_y_first);
		left = jb->line_left + dx;
		top = jb->line_bottom + dy;
		bottom = top + bm->h - 1;
		right = left + bm->w - 1;
		jb->line_left = left;
		jb->line_bottom = bottom;
		jb->prev_right = right;
		jb->prev_bottom = bottom;
		jb2_set_baseline(jb, bottom);
	} else {
		dx = jb2_decode_num(jb, BIGNEG, BIGPOS, &jb->rel_loc_x_prev);
		dy = -jb2_decode_num(jb, BIGNEG, BIGPOS, &jb->rel_loc_y_prev);
		left = jb->prev_right + dx;
		bottom = jb->prev_bottom + dy;
		top = bottom - bm->h + 1;
		right = left + bm->w - 1;
		jb->prev_right = right;
		jb->prev_bottom = jb2_update_baseline(jb, bottom);
	}
	*xp = left;
	*yp = top;
}

static void
jb2_decode_start_of_data(struct jb2_decoder *jb)
{
	int w = jb2_decode_num(jb, 0, BIGPOS, &jb->image_size_dist);
	int h = jb2_decode_num(jb, 0, BIGPOS, &jb->image_size_dist);
	int r = zp_decode(&jb->zp, &jb->dist_refinement_flag);
	printf("jb2: start-of-data %dx%d refine=%d\n", w, h, r);
	jb->page = jb2_new_bitmap(w, h);
	memset(jb->page->data, 255, w * h);
}

static void
jb2_decode_new_symbol(struct jb2_decoder *jb)
{
	struct bitmap *bm;
	int w, h, x, y;
	w = jb2_decode_num(jb, 0, BIGPOS, &jb->abs_size_x);
	h = jb2_decode_num(jb, 0, BIGPOS, &jb->abs_size_y);
	bm = jb2_decode_bitmap_direct(jb, w, h);
	jb2_decode_rel_loc(jb, bm, &x, &y);
	printf("jb2: new-symbol %dx%d %d,%d\n", w, h, x, y);
	jb2_blit_bitmap(jb->page, bm, x, y);
	jb2_add_to_library(jb, bm);
}

static void
jb2_decode_matched_refine(struct jb2_decoder *jb)
{
	struct bitmap *bm;
	int s, w, h, x, y;
	s = jb2_decode_num(jb, 0, jb->symlen - 1, &jb->dist_match_index);
	w = jb2_decode_num(jb, BIGNEG, BIGPOS, &jb->rel_size_x);
	h = jb2_decode_num(jb, BIGNEG, BIGPOS, &jb->rel_size_y);
	bm = jb2_decode_bitmap_refine(jb, s, w, h);
	jb2_decode_rel_loc(jb, bm, &x, &y);
	printf("jb2: matched-refine s=%d %dx%d %d,%d\n", s, w, h, x, y);
	jb2_blit_bitmap(jb->page, bm, x, y);
	jb2_add_to_library(jb, bm);
}

static void
jb2_decode_matched_copy(struct jb2_decoder *jb)
{
	struct bitmap *bm;
	int s, x, y;
	s = jb2_decode_num(jb, 0, jb->symlen - 1, &jb->dist_match_index);
	bm = jb->symbol[s];
	jb2_decode_rel_loc(jb, bm, &x, &y);
	printf("jb2: matched-copy s=%d %d,%d\n", s, x, y);
	jb2_blit_bitmap(jb->page, bm, x, y);
}

static int
jb2_decode_record(struct jb2_decoder *jb)
{
	int rectype;

	rectype = jb2_decode_num(jb, START_OF_DATA, END_OF_DATA, &jb->dist_record_type);
	printf("jb2: rectype=%d\n", rectype);

	switch (rectype) {
	case START_OF_DATA: jb2_decode_start_of_data(jb); break;
	case NEW_SYMBOL: jb2_decode_new_symbol(jb); break;
	case MATCHED_COPY: jb2_decode_matched_copy(jb); break;
	case MATCHED_REFINE: jb2_decode_matched_refine(jb); break;
	case END_OF_DATA: printf("jb2: end-of-data\n"); return 1;
	default: printf("jb2: unimplemented record type\n"); return 1;
	}

	return 0;
}

void
jb2_decode(unsigned char *src, int srclen)
{
	struct jb2_decoder jbx, *jb = &jbx;

	zp_init(&jb->zp, src, srclen);

	jb->len = 20500;
	jb->bit = malloc(jb->len);
	jb->left = malloc(jb->len * sizeof(unsigned int));
	jb->right = malloc(jb->len * sizeof(unsigned int));

	jb2_reset_num_coder(jb);

	jb->dist_refinement_flag = 0;
	jb->offset_type_dist = 0;

	memset(jb->direct, 0, sizeof jb->direct);
	memset(jb->refine, 0, sizeof jb->refine);

	jb->symlen = 0;
	jb->symcap = 0;
	jb->symbol = NULL;

	jb->line_left = 0;
	jb->line_bottom = 0;
	jb->prev_right = 0;
	jb->prev_bottom = 0;
	jb2_set_baseline(jb, 0);

	while (1)
		if (jb2_decode_record(jb))
			break;

	FILE *f = fopen("out.pgm", "wb");
	fprintf(f, "P5\n%d %d\n255\n", jb->page->w, jb->page->h);
	fwrite(jb->page->data, jb->page->w, jb->page->h, f);
	fclose(f);
}
