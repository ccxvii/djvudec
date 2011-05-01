#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct zp_decoder {
	unsigned int a, c, f, buffer, avail;
	unsigned char *rp, *ep;
};

void zp_init(struct zp_decoder *zp, unsigned char *data, int len);
int zp_decode(struct zp_decoder *zp, unsigned char *ctx);
int zp_decode_pass_through(struct zp_decoder *zp);

unsigned char *bzz_decode(unsigned char *src, int srclen, int *outlen);

struct jb2_decoder * jb2_new_decoder(unsigned char *src, int srclen, struct jb2_decoder *dict);
int jb2_decode(struct jb2_decoder *jb);
void jb2_print_page(struct jb2_decoder *jb);
void jb2_free_decoder(struct jb2_decoder *jb);

struct dv_document
{
	FILE *file;
	struct jb2_decoder *dict;
};

