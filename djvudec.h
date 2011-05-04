#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct djvu_bitmap {
	int w, h, stride;
	unsigned char *data;
};

struct djvu_pixmap {
	int w, h, n, stride;
	unsigned char *data;
};

struct djvu_bitmap *djvu_new_bitmap(int w, int h);
struct djvu_pixmap *djvu_new_pixmap(int w, int h, int n);

void djvu_free_bitmap(struct djvu_bitmap *);
void djvu_free_pixmap(struct djvu_pixmap *);

void djvu_write_bitmap(struct djvu_bitmap *bm, char *filename);
void djvu_write_pixmap(struct djvu_pixmap *pix, char *filename);

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

struct jb2palette *jb2_decode_palette(unsigned char *src, int len);
struct jb2library *jb2_decode_library(unsigned char *src, int len);
struct djvu_bitmap *jb2_decode_bitmap(unsigned char *src, int len, struct jb2library *shrlib);
struct djvu_pixmap *jb2_decode_pixmap(unsigned char *src, int len, struct jb2library *shrlib, struct jb2palette *pal);

void jb2_free_palette(struct jb2palette *pal);
void jb2_free_library(struct jb2library *lib);

/* IW44 wavelet image decoder */

/* DjVu document */

