#include "djvudec.h"

struct djvu_bitmap *
djvu_new_bitmap(int w, int h)
{
	struct djvu_bitmap *bm;
	bm = malloc(sizeof(struct djvu_bitmap));
	bm->w = w;
	bm->h = h;
	bm->stride = (w + 7) >> 3;
	bm->data = malloc(bm->h * bm->stride);
	memset(bm->data, 0, bm->h * bm->stride);
	return bm;
}

struct djvu_pixmap *
djvu_new_pixmap(int w, int h, int n)
{
	struct djvu_pixmap *pix;
	pix = malloc(sizeof(struct djvu_pixmap));
	pix->w = w;
	pix->h = h;
	pix->n = n;
	pix->stride = w * n;
	pix->data = malloc(pix->h * pix->stride);
	memset(pix->data, 255, pix->h * pix->stride);
	return pix;
}

void djvu_free_bitmap(struct djvu_bitmap *bm)
{
	free(bm->data);
	free(bm);
}

void djvu_free_pixmap(struct djvu_pixmap *pix)
{
	free(pix->data);
	free(pix);
}

void
djvu_write_bitmap(struct djvu_bitmap *bm, char *filename)
{
	FILE *f = fopen(filename, "wb");
	fprintf(f, "P4\n%d %d\n", bm->w, bm->h);
	fwrite(bm->data, bm->stride, bm->h, f);
	fclose(f);
}

void
djvu_write_pixmap(struct djvu_pixmap *pix, char *filename)
{
	FILE *f = fopen(filename, "wb");
	if (pix->n == 1)
		fprintf(f, "P5\n%d %d\n255\n", pix->w, pix->h);
	else
		fprintf(f, "P6\n%d %d\n255\n", pix->w, pix->h);
	fwrite(pix->data, pix->stride, pix->h, f);
	fclose(f);
}
