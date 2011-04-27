#include "mudjvu.h"

static inline unsigned int decode_raw(struct zp *zp, int b)
{
	int n = 1;
	int m = 1 << b;
	while (n < m)
		n = (n << 1) | zp_decode_pass_through(zp);
	return n - m;
}

static inline unsigned int decode_bin(struct zp *zp, unsigned char *ctx, int b)
{
	int n = 1;
	int m = 1 << b;
	ctx = ctx - 1;
	while (n < m)
		n = (n << 1) | zp_decode(zp, ctx + n);
	return n - m;
}

static int
dv_decode_bzz_block(struct zp *zp, unsigned char *ctx,
	unsigned char **out, int *outlen)
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

	/* phase 1: arithmetic decoding */

	/* decode block size */
	blocksize = decode_raw(zp, 24);

	data = malloc(blocksize);
	posc = malloc(blocksize);
	posn = malloc(blocksize * sizeof(int));

	memset(posn, 0, blocksize * sizeof(int));

	/* decode estimation speed */
	fshift = 0;
	if (zp_decode_pass_through(zp))
		if (zp_decode_pass_through(zp))
			fshift = 2;
		else
			fshift = 1;

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
			mtfno = 2 + decode_bin(zp, ctx + 7, 1);
			data[i] = mtf[mtfno];
		} else if (zp_decode(zp, ctx + 8)) {
			mtfno = 4 + decode_bin(zp, ctx + 9, 2);
			data[i] = mtf[mtfno];
		} else if (zp_decode(zp, ctx + 12)) {
			mtfno = 8 + decode_bin(zp, ctx + 13, 3);
			data[i] = mtf[mtfno];
		} else if (zp_decode(zp, ctx + 20)) {
			mtfno = 16 + decode_bin(zp, ctx + 21, 4);
			data[i] = mtf[mtfno];
		} else if (zp_decode(zp, ctx + 36)) {
			mtfno = 32 + decode_bin(zp, ctx + 37, 5);
			data[i] = mtf[mtfno];
		} else if (zp_decode(zp, ctx + 68)) {
			mtfno = 64 + decode_bin(zp, ctx + 69, 6);
			data[i] = mtf[mtfno];
		} else if (zp_decode(zp, ctx + 132)) {
			mtfno = 128 + decode_bin(zp, ctx + 133, 7);
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

	*out = data;
	*outlen = blocksize - 1;

	free(posc);
	free(posn);
	return 0;

error:
	free(data);
	free(posc);
	free(posn);
	return fz_throw("bzz decoding error");
}

int
dv_decode_bzz(unsigned char **out, int *outlen, unsigned char *src, int srclen)
{
	struct zp zp;
	unsigned char ctx[260];
	zp_init(&zp, src, srclen);
	memset(ctx, 0, sizeof ctx);
	return dv_decode_bzz_block(&zp, ctx, out, outlen);
}
