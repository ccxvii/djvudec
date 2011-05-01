#include "djvudec.h"

static unsigned int
bzz_decode_raw(struct zpdec *zp, int b)
{
	int n = 1;
	int m = 1 << b;
	while (n < m)
		n = (n << 1) | zp_decode_pass_through(zp);
	return n - m;
}

static unsigned int
bzz_decode_bin(struct zpdec *zp, unsigned char *ctx, int b)
{
	int n = 1;
	int m = 1 << b;
	ctx = ctx - 1;
	while (n < m)
		n = (n << 1) | zp_decode(zp, ctx + n);
	return n - m;
}

static unsigned char *
bzz_decode_block(struct zpdec *zp, unsigned char *ctx, int *outlen)
{
	unsigned char mtf[256];
	int count[256];
	int blocksize;
	int fshift, fadd, mtfno;
	int freq[4];
	int markerpos;
	int ctxid, fc;
	int i, k, last;

	unsigned char *data;
	unsigned char *posc;
	int *posn;

	*outlen = 0;

	/* phase 1: arithmetic decoding */

	/* decode block size */
	blocksize = bzz_decode_raw(zp, 24);

	if (blocksize == 0 || blocksize > 4096 * 1024)
		return NULL;

	data = malloc(blocksize);
	posc = malloc(blocksize);
	posn = malloc(blocksize * sizeof(int));

	memset(posn, 0, blocksize * sizeof(int));

	/* decode estimation speed */
	fshift = 0;
	if (zp_decode_pass_through(zp)) {
		if (zp_decode_pass_through(zp))
			fshift = 2;
		else
			fshift = 1;
	}

	/* fill mtf array */
	for (i = 0; i < 256; i++)
		mtf[i] = i;

	/* decode */
	mtfno = 3;
	markerpos = -1;
	fadd = 4;
	for (i = 0; i < 4; i++)
		freq[i] = 0;

	for (i = 0; i < blocksize; i++) {
		ctxid = 2;
		if (ctxid > mtfno)
			ctxid = mtfno;

		if (ctxid < 0 || ctxid > 2)
			goto error;

		if (zp_decode(zp, ctx + ctxid)) {
			mtfno = 0;
			data[i] = mtf[mtfno];
		} else if (zp_decode(zp, ctx + ctxid + 3)) {
			mtfno = 1;
			data[i] = mtf[mtfno];
		} else if (zp_decode(zp, ctx + 6)) {
			mtfno = 2 + bzz_decode_bin(zp, ctx + 7, 1);
			data[i] = mtf[mtfno];
		} else if (zp_decode(zp, ctx + 8)) {
			mtfno = 4 + bzz_decode_bin(zp, ctx + 9, 2);
			data[i] = mtf[mtfno];
		} else if (zp_decode(zp, ctx + 12)) {
			mtfno = 8 + bzz_decode_bin(zp, ctx + 13, 3);
			data[i] = mtf[mtfno];
		} else if (zp_decode(zp, ctx + 20)) {
			mtfno = 16 + bzz_decode_bin(zp, ctx + 21, 4);
			data[i] = mtf[mtfno];
		} else if (zp_decode(zp, ctx + 36)) {
			mtfno = 32 + bzz_decode_bin(zp, ctx + 37, 5);
			data[i] = mtf[mtfno];
		} else if (zp_decode(zp, ctx + 68)) {
			mtfno = 64 + bzz_decode_bin(zp, ctx + 69, 6);
			data[i] = mtf[mtfno];
		} else if (zp_decode(zp, ctx + 132)) {
			mtfno = 128 + bzz_decode_bin(zp, ctx + 133, 7);
			data[i] = mtf[mtfno];
		} else {
			mtfno = 256; /* EOB symbol */
			data[i] = 0;
			markerpos = i;
		}

		if (mtfno < 256) {
			/* update frequencies */
			fadd = fadd + (fadd >> fshift);
			if (fadd > 0x10000000) {
				fadd >>= 24;
				for (k = 0; k < 4; k++)
					freq[k] = freq[k]>>24;
			}
			if (mtfno < 4)
				fc = fadd + freq[mtfno];
			else
				fc = fadd;

			/* rotate mtf */
			k = mtfno;
			while (k > 3) {
				mtf[k] = mtf[k - 1];
				k--;
			}
			while (k > 0 && fc >= freq[k-1]) {
				mtf[k] = mtf[k - 1];
				freq[k] = freq[k - 1];
				k--;
			}
			mtf[k] = data[i];
			freq[k] = fc;
		}
	}

	if (markerpos < 0 || markerpos >= blocksize)
		goto error;

	/* phase 2: inverse burrows wheeler transform */

	for (i = 0; i < 256; i++)
		count[i] = 0;

	for (i = 0; i < blocksize; i++) {
		k = data[i];
		posc[i] = k;
		if (i == markerpos)
			posn[i] = 0;
		else {
			posn[i] = count[k];
			count[k] = count[k] + 1;
		}
	}

	last = 1;
	for (i = 0; i < 256; i++) {
		k = count[i];
		count[i] = last;
		last = last + k;
	}

	if (last != blocksize)
		goto error;

	k = 0;
	last = blocksize - 1;
	while (last > 0) {
		last--;
		data[last] = posc[k];
		k = count[posc[k]] + posn[k];
	}

	if (k != markerpos)
		goto error;

	*outlen = blocksize - 1;

	free(posc);
	free(posn);
	return data;

error:
	free(posc);
	free(posn);
	free(data);
	return NULL;
}

unsigned char *
bzz_decode(unsigned char *src, int srclen, int *outlen)
{
	struct zpdec zp;
	unsigned char ctx[260];
	zp_init(&zp, src, srclen);
	memset(ctx, 0, sizeof ctx);
	return bzz_decode_block(&zp, ctx, outlen);
}
