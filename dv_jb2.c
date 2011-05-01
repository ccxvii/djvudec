#include "djvudec.h"

enum {
	BIGPOS = 262142,
	BIGNEG = -262143,
	CHUNK = 20000,
};

enum {
	START_OF_DATA = 0,
	NEW_SYMBOL = 1,
	NEW_SYMBOL_LIBRARY = 2,
	NEW_SYMBOL_IMAGE = 3,
	MATCHED_REFINE = 4,
	MATCHED_REFINE_LIBRARY = 5,
	MATCHED_REFINE_IMAGE = 6,
	MATCHED_COPY = 7,
	NON_SYMBOL_DATA = 8,
	DICT_OR_RESET = 9,
	COMMENT = 10,
	END_OF_DATA = 11
};

struct bitmap {
	int w, h;
	int stride;
	unsigned char *data;
	unsigned char *orig;
};

struct jb2dec {
	struct zpdec zp;

	/* symbol library */
	int symlen, symcap, symshr;
	struct bitmap **symbol;

	/* relative locations */
	int new_left, new_bottom;
	int same_right, same_bottom;
	int baseline[3], baseline_pos;

	/* number coder */
	int numlen, numcap;
	unsigned char *bit;
	unsigned int *left;
	unsigned int *right;

	/* number contexts */
	unsigned int record_type;
	unsigned int image_size;
	unsigned int match_index;
	unsigned int abs_size_x;
	unsigned int abs_size_y;
	unsigned int rel_size_x;
	unsigned int rel_size_y;
	unsigned int abs_loc_x;
	unsigned int abs_loc_y;
	unsigned int rel_loc_x_same;
	unsigned int rel_loc_y_same;
	unsigned int rel_loc_x_new;
	unsigned int rel_loc_y_new;
	unsigned int comment_length;
	unsigned int comment_octet;
	unsigned int inherited_shape_count;

	/* bit contexts */
	unsigned char refinement_flag;
	unsigned char offset_type;

	/* bitmap contexts */
	unsigned char direct[1024];
	unsigned char refine[2048];

	/* output page */
	int started;
	struct bitmap *page;
	struct jb2dec *dict;
};

static void
jb2_grow_num_coder(struct jb2dec *jb)
{
	int newcap = jb->numcap + CHUNK;
	jb->bit = realloc(jb->bit, newcap);
	jb->left = realloc(jb->left, newcap * sizeof(unsigned int));
	jb->right = realloc(jb->right, newcap * sizeof(unsigned int));
	memset(jb->bit + jb->numcap, 0, CHUNK);
	memset(jb->left + jb->numcap, 0, CHUNK * sizeof(unsigned int));
	memset(jb->right + jb->numcap, 0, CHUNK * sizeof(unsigned int));
	jb->numcap = newcap;
}

static void
jb2_reset_num_coder(struct jb2dec *jb)
{
	jb->comment_octet = 0;
	jb->comment_length = 0;
	jb->record_type = 0;
	jb->match_index = 0;
	jb->abs_loc_x = 0;
	jb->abs_loc_y = 0;
	jb->abs_size_x = 0;
	jb->abs_size_y = 0;
	jb->image_size = 0;
	jb->inherited_shape_count = 0;
	jb->rel_loc_x_new = 0;
	jb->rel_loc_y_new = 0;
	jb->rel_loc_x_same = 0;
	jb->rel_loc_y_same = 0;
	jb->rel_size_x = 0;
	jb->rel_size_y = 0;

	memset(jb->bit, 0, jb->numcap);
	memset(jb->left, 0, jb->numcap * sizeof(unsigned int));
	memset(jb->right, 0, jb->numcap * sizeof(unsigned int));
	jb->numlen = 1;
}

static int
jb2_decode_num(struct jb2dec *jb, int low, int high, unsigned int *ctx)
{
	int negative, cutoff, phase, range, decision;

	negative = 0;
	cutoff = 0;
	phase = 1;
	range = 0xffffffff;

	while (range != 1) {
		if (!*ctx) {
			if (jb->numlen >= jb->numcap)
				jb2_grow_num_coder(jb);
			*ctx = jb->numlen++;
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
jb2_blit_bitmap(struct jb2dec *jb, struct bitmap *src, int dx, int dy)
{
	int x, y;
	if (!jb->page)
		return;
	for (y = 0; y < src->h; y++) {
		for (x = 0; x < src->w; x++) {
			if (getbit(src, x, y))
				setbit(jb->page, dx+x, dy+y, 0);
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
jb2_add_to_library(struct jb2dec *jb, struct bitmap *bm)
{
	if (jb->symlen + 1 >= jb->symcap) {
		jb->symcap += 1000;
		jb->symbol = realloc(jb->symbol, jb->symcap * sizeof(struct bitmap*));
	}
	jb2_trim_edges(bm);
	jb->symbol[jb->symlen++] = bm;
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
jb2_decode_bitmap_direct(struct jb2dec *jb, int w, int h)
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

	return bm;
}

static struct bitmap *
jb2_decode_bitmap_refine(struct jb2dec *jb, int s, int dw, int dh)
{
	struct bitmap *bm, *sm;
	int w, h, x, y, dx, dy, ctx, v;

	sm = jb->symbol[s];
	w = sm->w + dw;
	h = sm->h + dh;
	dx = (sm->w-1) / 2 - (w-1) / 2;
	dy = sm->h / 2 - h / 2;

	bm = jb2_new_bitmap(w, h);

	for (y = 0; y < h; y++) {
		ctx = jb2_refine_context(bm, 0, y, sm, dx, y+dy);
		for (x = 0; x < w; x++) {
			v = zp_decode(&jb->zp, jb->refine + ctx);
			setbit(bm, x, y, v);
			ctx = jb2_shift_refine_context(bm, x+1, y,
					sm, x+dx+1, y+dy, ctx, v);
		}
	}

	return bm;
}

static void
jb2_set_baseline(struct jb2dec *jb, int y)
{
	jb->baseline[0] = jb->baseline[1] = jb->baseline[2] = y;
	jb->baseline_pos = 0;
}

static int
jb2_update_baseline(struct jb2dec *jb, int y)
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
jb2_decode_rel_loc(struct jb2dec *jb, struct bitmap *bm, int *xp, int *yp)
{
	int dx, dy, left, right, top, bottom, t;
	t = zp_decode(&jb->zp, &jb->offset_type);
	if (t) {
		dx = jb2_decode_num(jb, BIGNEG, BIGPOS, &jb->rel_loc_x_new);
		dy = -jb2_decode_num(jb, BIGNEG, BIGPOS, &jb->rel_loc_y_new);
		left = jb->new_left + dx;
		top = jb->new_bottom + dy;
		bottom = top + bm->h - 1;
		right = left + bm->w - 1;
		jb->new_left = left;
		jb->new_bottom = bottom;
		jb->same_right = right;
		jb->same_bottom = bottom;
		jb2_set_baseline(jb, bottom);
	} else {
		dx = jb2_decode_num(jb, BIGNEG, BIGPOS, &jb->rel_loc_x_same);
		dy = -jb2_decode_num(jb, BIGNEG, BIGPOS, &jb->rel_loc_y_same);
		left = jb->same_right + dx;
		bottom = jb->same_bottom + dy;
		top = bottom - bm->h + 1;
		right = left + bm->w - 1;
		jb->same_right = right;
		jb->same_bottom = jb2_update_baseline(jb, bottom);
	}
	*xp = left;
	*yp = top;
}

static void
jb2_decode_start_of_data(struct jb2dec *jb)
{
	int w = jb2_decode_num(jb, 0, BIGPOS, &jb->image_size);
	int h = jb2_decode_num(jb, 0, BIGPOS, &jb->image_size);
	(void) zp_decode(&jb->zp, &jb->refinement_flag);
	jb->started = 1;
	if (w && h) {
		jb->page = jb2_new_bitmap(w, h);
		memset(jb->page->data, 255, w * h);
	}
}

static int
jb2_decode_dict_or_reset(struct jb2dec *jb)
{
	if (!jb->started) {
		int i, count;
		count = jb2_decode_num(jb, 0, BIGPOS, &jb->inherited_shape_count);
		if (!jb->dict || jb->dict->symlen < count || jb->symlen > 0)
			return -1;
		for (i = 0; i < count; i++)
			jb2_add_to_library(jb, jb->dict->symbol[i]);
		jb->symshr = count;
	} else {
		jb2_reset_num_coder(jb);
	}
	return 0;
}

static void
jb2_decode_new_symbol(struct jb2dec *jb, int doimg, int dolib)
{
	struct bitmap *bm;
	int w, h, x, y;
	w = jb2_decode_num(jb, 0, BIGPOS, &jb->abs_size_x);
	h = jb2_decode_num(jb, 0, BIGPOS, &jb->abs_size_y);
	bm = jb2_decode_bitmap_direct(jb, w, h);
	if (doimg) {
		jb2_decode_rel_loc(jb, bm, &x, &y);
		jb2_blit_bitmap(jb, bm, x, y);
	}
	if (dolib)
		jb2_add_to_library(jb, bm);
}

static void
jb2_decode_matched_refine(struct jb2dec *jb, int doimg, int dolib)
{
	struct bitmap *bm;
	int s, w, h, x, y;
	s = jb2_decode_num(jb, 0, jb->symlen - 1, &jb->match_index);
	w = jb2_decode_num(jb, BIGNEG, BIGPOS, &jb->rel_size_x);
	h = jb2_decode_num(jb, BIGNEG, BIGPOS, &jb->rel_size_y);
	bm = jb2_decode_bitmap_refine(jb, s, w, h);
	if (doimg) {
		jb2_decode_rel_loc(jb, bm, &x, &y);
		jb2_blit_bitmap(jb, bm, x, y);
	}
	if (dolib)
		jb2_add_to_library(jb, bm);
}

static void
jb2_decode_matched_copy(struct jb2dec *jb)
{
	struct bitmap *bm;
	int s, x, y;
	s = jb2_decode_num(jb, 0, jb->symlen - 1, &jb->match_index);
	bm = jb->symbol[s];
	jb2_decode_rel_loc(jb, bm, &x, &y);
	jb2_blit_bitmap(jb, bm, x, y);
}

static void
jb2_decode_non_symbol_data(struct jb2dec *jb)
{
	struct bitmap *bm;
	int x, y, w, h;
	fprintf(stderr, "jb2: non-symbol-data\n");
	w = jb2_decode_num(jb, 0, BIGPOS, &jb->abs_size_x);
	h = jb2_decode_num(jb, 0, BIGPOS, &jb->abs_size_y);
	bm = jb2_decode_bitmap_direct(jb, w, h);
	x = jb2_decode_num(jb, 1, BIGPOS, &jb->abs_loc_x) - 1;
	y = jb2_decode_num(jb, 1, BIGPOS, &jb->abs_loc_y) - 1;
	jb2_blit_bitmap(jb, bm, x, y);
	jb2_free_bitmap(bm);
}

static void
jb2_decode_comment(struct jb2dec *jb)
{
	int n = jb2_decode_num(jb, 0, BIGPOS, &jb->comment_length);
	while (n--)
		jb2_decode_num(jb, 0, 255, &jb->comment_octet);
}

int
jb2_decode(struct jb2dec *jb)
{
	int rec;
	while (1) {
		rec = jb2_decode_num(jb, 0, END_OF_DATA, &jb->record_type);
		switch (rec) {
		case START_OF_DATA:
			jb2_decode_start_of_data(jb);
			break;
		case NEW_SYMBOL:
			jb2_decode_new_symbol(jb, 1, 1);
			break;
		case NEW_SYMBOL_LIBRARY:
			jb2_decode_new_symbol(jb, 0, 1);
			break;
		case NEW_SYMBOL_IMAGE:
			jb2_decode_new_symbol(jb, 1, 0);
			break;
		case MATCHED_REFINE:
			jb2_decode_matched_refine(jb, 1, 1);
			break;
		case MATCHED_REFINE_LIBRARY:
			jb2_decode_matched_refine(jb, 0, 1);
			break;
		case MATCHED_REFINE_IMAGE:
			jb2_decode_matched_refine(jb, 1, 0);
			break;
		case MATCHED_COPY:
			jb2_decode_matched_copy(jb);
			break;
		case NON_SYMBOL_DATA:
			jb2_decode_non_symbol_data(jb);
			break;
		case DICT_OR_RESET:
			if (jb2_decode_dict_or_reset(jb))
				return -1;
			break;
		case COMMENT:
			jb2_decode_comment(jb);
			break;
		case END_OF_DATA:
			return 0;
		default:
			fprintf(stderr, "jb2: unimplemented record type\n");
			return -1;
		}
	}
}

struct jb2dec *
jb2_new_decoder(unsigned char *src, int srclen, struct jb2dec *dict)
{
	struct jb2dec *jb;

	jb = malloc(sizeof(struct jb2dec));

	zp_init(&jb->zp, src, srclen);

	jb->numcap = 20500;
	jb->bit = malloc(jb->numcap);
	jb->left = malloc(jb->numcap * sizeof(unsigned int));
	jb->right = malloc(jb->numcap * sizeof(unsigned int));

	jb2_reset_num_coder(jb);

	jb->refinement_flag = 0;
	jb->offset_type = 0;

	memset(jb->direct, 0, sizeof jb->direct);
	memset(jb->refine, 0, sizeof jb->refine);

	jb->symlen = 0;
	jb->symcap = 0;
	jb->symshr = 0;
	jb->symbol = NULL;

	jb->new_left = 0;
	jb->new_bottom = 0;
	jb->same_right = 0;
	jb->same_bottom = 0;
	jb2_set_baseline(jb, 0);

	jb->started = 0;
	jb->page = NULL;
	jb->dict = dict;

	return jb;
}

void
jb2_print_page(struct jb2dec *jb)
{
	FILE *f = fopen("out.pgm", "wb");
	fprintf(f, "P5\n%d %d\n255\n", jb->page->w, jb->page->h);
	fwrite(jb->page->data, jb->page->w, jb->page->h, f);
	fclose(f);
}

void
jb2_free_decoder(struct jb2dec *jb)
{
	int i;

	free(jb->bit);
	free(jb->left);
	free(jb->right);

	for (i = jb->symshr; i < jb->symlen; i++)
		jb2_free_bitmap(jb->symbol[i]);
	free(jb->symbol);

	jb2_free_bitmap(jb->page);

	free(jb);
}
