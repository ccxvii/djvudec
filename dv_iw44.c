#include "djvudec.h"

struct iw44 {
	int w, h;
};

void
iw44_decode(unsigned char *src, int srclen)
{
	struct zpdec zp;
	unsigned char ctx[260];
	zp_init(&zp, src, srclen);
	memset(ctx, 0, sizeof ctx);
}
