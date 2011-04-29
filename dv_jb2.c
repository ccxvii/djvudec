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

struct jb2_decoder {
	struct zp_decoder zp;

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
	unsigned int rel_loc_x_current;
	unsigned int rel_loc_x_last;
	unsigned int rel_loc_y_current;
	unsigned int rel_loc_y_last;
	unsigned int rel_size_x;
	unsigned int rel_size_y;

	/* bit contexts */
	unsigned char dist_refinement_flag;
	unsigned char offset_type_dist;

	/* library */
	int hbound;
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
	jb->rel_loc_x_current = 0;
	jb->rel_loc_x_last = 0;
	jb->rel_loc_y_current = 0;
	jb->rel_loc_y_last = 0;
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

int
jb2_decode_eventual_image_refinement_flag(struct jb2_decoder *jb)
{
	return zp_decode(&jb->zp, &jb->dist_refinement_flag);
}

void
jb2_decode_start_of_data(struct jb2_decoder *jb)
{
	int w = jb2_decode_num(jb, 0, BIGPOS, &jb->image_size_dist);
	int h = jb2_decode_num(jb, 0, BIGPOS, &jb->image_size_dist);
	int r = jb2_decode_eventual_image_refinement_flag(jb);
	printf("jb2: start-of-data %dx%d refine=%d\n", w, h, r);
}

void
jb2_decode_new_symbol(struct jb2_decoder *jb)
{
	int w = jb2_decode_num(jb, 0, BIGPOS, &jb->abs_size_x);
	int h = jb2_decode_num(jb, 0, BIGPOS, &jb->abs_size_y);
	printf("jb2: new-symbol %dx%d\n", w, h);
}

void
jb2_decode_matched_refine(struct jb2_decoder *jb)
{
	int match = jb2_decode_num(jb, 0, jb->hbound, &jb->dist_match_index);

	int xdiff = jb2_decode_num(jb, BIGNEG, BIGPOS, &jb->rel_size_x);
	int ydiff = jb2_decode_num(jb, BIGNEG, BIGPOS, &jb->rel_size_y);

	printf("jb2: matched refine m=%d %dx%d\n", match, xdiff, ydiff);

//	code_match_index
//	code_relative_symbol_size
//	code_bitmap_by_cross_coding
//	code_relative_location
}

int
jb2_decode_record(struct jb2_decoder *jb)
{
	int rectype;

	rectype = jb2_decode_num(jb, START_OF_DATA, END_OF_DATA, &jb->dist_record_type);
	printf("jb2: rectype=%d\n", rectype);

	switch (rectype) {
	case START_OF_DATA: jb2_decode_start_of_data(jb); break;
	case NEW_SYMBOL: jb2_decode_new_symbol(jb); break;
	case MATCHED_REFINE: jb2_decode_matched_refine(jb); break;
	case END_OF_DATA: printf("jb2: end-of-data\n"); return 1;
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

	jb->hbound = 0;

	while (1)
		if (jb2_decode_record(jb))
			break;
}
