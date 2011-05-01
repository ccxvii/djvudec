#include "djvudec.h"

enum { ZERO=1, ACTIVE=2, NEW=4, UNK=8 }; /* bucket states */

struct iw44dec {
	struct zpdec zp;

	/* headers */
	unsigned char serial, slices;
	unsigned char major, minor;
	unsigned char xhi, xlo, yhi, ylo, crcbdelay;

	/* codec */
	int curband;
	int curbit;
	int quant_hi[10];
	int quant_lo[16];
	char coeffstate[256];
	char bucketstate[16];

	/* coding context */
	unsigned char ctx_start[32];
	unsigned char ctx_bucket[10][8];
	unsigned char ctx_mant;
	unsigned char ctx_root;
};

struct iw44image *
iw44_decode_image(unsigned char *src, int srclen)
{
	struct zpdec zp;
	unsigned char ctx[260];
	zp_init(&zp, src, srclen);
	memset(ctx, 0, sizeof ctx);
	return NULL;
}
