#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct zp {
	unsigned int a, c, fence, buffer, avail;
	unsigned char *rp, *ep;
	unsigned int p[256];
	unsigned int m[256];
	unsigned char up[256];
	unsigned char dn[256];
	unsigned char ffz[256];
};

void zp_init(struct zp *zp, unsigned char *data, int len);
int zp_decode(struct zp *zp, unsigned char *ctx);
int zp_decode_pass_through(struct zp *zp);

int dv_decode_bzz(unsigned char **out, int *outlen, unsigned char *src, int srclen);

struct dv_document
{
	FILE *file;
};

