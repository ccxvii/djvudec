#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct zpdec {
	unsigned int a, c, f, buffer, avail;
	unsigned char *rp, *ep;
};

void zp_init(struct zpdec *zp, unsigned char *data, int len);
int zp_decode(struct zpdec *zp, unsigned char *ctx);
int zp_decode_pass_through(struct zpdec *zp);

unsigned char *bzz_decode(unsigned char *src, int srclen, int *outlen);

void iw44_decode(unsigned char *src, int srclen);

struct jb2dec * jb2_new_decoder(unsigned char *src, int srclen, struct jb2dec *dict);
int jb2_decode(struct jb2dec *jb);
void jb2_print_page(struct jb2dec *jb);
void jb2_free_decoder(struct jb2dec *jb);

struct dv_document
{
	FILE *file;
	struct jb2dec *dict;
};

