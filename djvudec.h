#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Z' adaptive binary arithmetic coder */

struct zpdec {
	unsigned int a, c, f, buffer, avail;
	unsigned char *rp, *ep;
};

void zp_init(struct zpdec *zp, unsigned char *data, int len);
int zp_decode(struct zpdec *zp, unsigned char *ctx);
int zp_decode_pass_through(struct zpdec *zp);

/* BZZ Burrows-Wheeler general purpose compressor */

unsigned char *bzz_decode(unsigned char *src, int srclen, int *outlen);

/* JB2 bi-level image decoder */

struct jb2image {
	int w, h, stride;
	unsigned char *data;
};

struct jb2library *jb2_decode_library(unsigned char *src, int len);
struct jb2image *jb2_decode_image(unsigned char *src, int len, struct jb2library *lib);
void jb2_write_pbm(struct jb2image *img, char *filename);
void jb2_free_library(struct jb2library *lib);
void jb2_free_image(struct jb2image *img);

/* IW44 wavelet image decoder */

struct iw44image {
	int w, h, n;
	unsigned char *data;
};

struct iw44image *iw44_decode_image(unsigned char *src, int srclen);
void iw44_write_pnm(struct iw44image *img, char *filename);
void iw44_free_image(struct iw44image *img);

/* DjVu document */

