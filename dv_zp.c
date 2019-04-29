#include "djvudec.h"

/*
	The Z'-Coder is an approximate adaptive binary arithmetic coder.

	The Z'-Coder as listed in the DjVu spec is incomplete.
	The implemented Z'-Coder uses a fence, like the Z-Coder.
	Comparisons in the spec are often wrong, saying > where
	the implementation uses >=.

	The calculation of Z for the pass-through case is also different.
*/

static const unsigned short zp_tab_p[256] = {
	0x8000, 0x8000, 0x8000, 0x6bbd, 0x6bbd, 0x5d45, 0x5d45, 0x51b9,
	0x51b9, 0x4813, 0x4813, 0x3fd5, 0x3fd5, 0x38b1, 0x38b1, 0x3275,
	0x3275, 0x2cfd, 0x2cfd, 0x2825, 0x2825, 0x23ab, 0x23ab, 0x1f87,
	0x1f87, 0x1bbb, 0x1bbb, 0x1845, 0x1845, 0x1523, 0x1523, 0x1253,
	0x1253, 0x0fcf, 0x0fcf, 0x0d95, 0x0d95, 0x0b9d, 0x0b9d, 0x09e3,
	0x09e3, 0x0861, 0x0861, 0x0711, 0x0711, 0x05f1, 0x05f1, 0x04f9,
	0x04f9, 0x0425, 0x0425, 0x0371, 0x0371, 0x02d9, 0x02d9, 0x0259,
	0x0259, 0x01ed, 0x01ed, 0x0193, 0x0193, 0x0149, 0x0149, 0x010b,
	0x010b, 0x00d5, 0x00d5, 0x00a5, 0x00a5, 0x007b, 0x007b, 0x0057,
	0x0057, 0x003b, 0x003b, 0x0023, 0x0023, 0x0013, 0x0013, 0x0007,
	0x0007, 0x0001, 0x0001, 0x5695, 0x24ee, 0x8000, 0x0d30, 0x481a,
	0x0481, 0x3579, 0x017a, 0x24ef, 0x007b, 0x1978, 0x0028, 0x10ca,
	0x000d, 0x0b5d, 0x0034, 0x078a, 0x00a0, 0x050f, 0x0117, 0x0358,
	0x01ea, 0x0234, 0x0144, 0x0173, 0x0234, 0x00f5, 0x0353, 0x00a1,
	0x05c5, 0x011a, 0x03cf, 0x01aa, 0x0285, 0x0286, 0x01ab, 0x03d3,
	0x011a, 0x05c5, 0x00ba, 0x08ad, 0x007a, 0x0ccc, 0x01eb, 0x1302,
	0x02e6, 0x1b81, 0x045e, 0x24ef, 0x0690, 0x2865, 0x09de, 0x3987,
	0x0dc8, 0x2c99, 0x10ca, 0x3b5f, 0x0b5d, 0x5695, 0x078a, 0x8000,
	0x050f, 0x24ee, 0x0358, 0x0d30, 0x0234, 0x0481, 0x0173, 0x017a,
	0x00f5, 0x007b, 0x00a1, 0x0028, 0x011a, 0x000d, 0x01aa, 0x0034,
	0x0286, 0x00a0, 0x03d3, 0x0117, 0x05c5, 0x01ea, 0x08ad, 0x0144,
	0x0ccc, 0x0234, 0x1302, 0x0353, 0x1b81, 0x05c5, 0x24ef, 0x03cf,
	0x2b74, 0x0285, 0x201d, 0x01ab, 0x1715, 0x011a, 0x0fb7, 0x00ba,
	0x0a67, 0x01eb, 0x06e7, 0x02e6, 0x0496, 0x045e, 0x030d, 0x0690,
	0x0206, 0x09de, 0x0155, 0x0dc8, 0x00e1, 0x2b74, 0x0094, 0x201d,
	0x0188, 0x1715, 0x0252, 0x0fb7, 0x0383, 0x0a67, 0x0547, 0x06e7,
	0x07e2, 0x0496, 0x0bc0, 0x030d, 0x1178, 0x0206, 0x19da, 0x0155,
	0x24ef, 0x00e1, 0x320e, 0x0094, 0x432a, 0x0188, 0x447d, 0x0252,
	0x5ece, 0x0383, 0x8000, 0x0547, 0x481a, 0x07e2, 0x3579, 0x0bc0,
	0x24ef, 0x1178, 0x1978, 0x19da, 0x2865, 0x24ef, 0x3987, 0x320e,
	0x2c99, 0x432a, 0x3b5f, 0x447d, 0x5695, 0x5ece, 0x8000, 0x8000,
	0x5695, 0x481a, 0x481a,
};

static const unsigned short zp_tab_m[256] = {
	0x0000, 0x0000, 0x0000, 0x10a5, 0x10a5, 0x1f28, 0x1f28, 0x2bd3,
	0x2bd3, 0x36e3, 0x36e3, 0x408c, 0x408c, 0x48fd, 0x48fd, 0x505d,
	0x505d, 0x56d0, 0x56d0, 0x5c71, 0x5c71, 0x615b, 0x615b, 0x65a5,
	0x65a5, 0x6962, 0x6962, 0x6ca2, 0x6ca2, 0x6f74, 0x6f74, 0x71e6,
	0x71e6, 0x7404, 0x7404, 0x75d6, 0x75d6, 0x7768, 0x7768, 0x78c2,
	0x78c2, 0x79ea, 0x79ea, 0x7ae7, 0x7ae7, 0x7bbe, 0x7bbe, 0x7c75,
	0x7c75, 0x7d0f, 0x7d0f, 0x7d91, 0x7d91, 0x7dfe, 0x7dfe, 0x7e5a,
	0x7e5a, 0x7ea6, 0x7ea6, 0x7ee6, 0x7ee6, 0x7f1a, 0x7f1a, 0x7f45,
	0x7f45, 0x7f6b, 0x7f6b, 0x7f8d, 0x7f8d, 0x7faa, 0x7faa, 0x7fc3,
	0x7fc3, 0x7fd7, 0x7fd7, 0x7fe7, 0x7fe7, 0x7ff2, 0x7ff2, 0x7ffa,
	0x7ffa, 0x7fff, 0x7fff, 0x0000,
};

static const unsigned char zp_tab_up[256] = {
	84, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18,
	19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34,
	35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50,
	51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66,
	67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82,
	81, 82, 9, 86, 5, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 82,
	99, 76, 101, 70, 103, 66, 105, 106, 107, 66, 109, 60, 111, 56,
	69, 114, 65, 116, 61, 118, 57, 120, 53, 122, 49, 124, 43, 72,
	39, 60, 33, 56, 29, 52, 23, 48, 23, 42, 137, 38, 21, 140, 15,
	142, 9, 144, 141, 146, 147, 148, 149, 150, 151, 152, 153, 154,
	155, 70, 157, 66, 81, 62, 75, 58, 69, 54, 65, 50, 167, 44, 65,
	40, 59, 34, 55, 30, 175, 24, 177, 178, 179, 180, 181, 182, 183,
	184, 69, 186, 59, 188, 55, 190, 51, 192, 47, 194, 41, 196, 37,
	198, 199, 72, 201, 62, 203, 58, 205, 54, 207, 50, 209, 46, 211,
	40, 213, 36, 215, 30, 217, 26, 219, 20, 71, 14, 61, 14, 57, 8,
	53, 228, 49, 230, 45, 232, 39, 234, 35, 138, 29, 24, 25, 240,
	19, 22, 13, 16, 13, 10, 7, 244, 249, 10, 89, 230,
};

static const unsigned char zp_tab_dn[256] = {
	145, 4, 3, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
	16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
	32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47,
	48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63,
	64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79,
	80, 85, 226, 6, 176, 143, 138, 141, 112, 135, 104, 133, 100,
	129, 98, 127, 72, 125, 102, 123, 60, 121, 110, 119, 108, 117,
	54, 115, 48, 113, 134, 59, 132, 55, 130, 51, 128, 47, 126, 41,
	62, 37, 66, 31, 54, 25, 50, 131, 46, 17, 40, 15, 136, 7, 32,
	139, 172, 9, 170, 85, 168, 248, 166, 247, 164, 197, 162, 95,
	160, 173, 158, 165, 156, 161, 60, 159, 56, 71, 52, 163, 48, 59,
	42, 171, 38, 169, 32, 53, 26, 47, 174, 193, 18, 191, 222, 189,
	218, 187, 216, 185, 214, 61, 212, 53, 210, 49, 208, 45, 206, 39,
	204, 195, 202, 31, 200, 243, 64, 239, 56, 237, 52, 235, 48, 233,
	44, 231, 38, 229, 34, 227, 28, 225, 22, 223, 16, 221, 220, 63,
	8, 55, 224, 51, 2, 47, 87, 43, 246, 37, 244, 33, 238, 27, 236,
	21, 16, 15, 8, 241, 242, 7, 10, 245, 2, 1, 83, 250, 2, 143, 246,
};

static inline unsigned int zp_read_byte(struct zp_decoder *zp)
{
	if (zp->rp < zp->ep)
		return *zp->rp++;
	return 0xff;
}

static inline int zp_read_bit(struct zp_decoder *zp)
{
	int b;
	zp->avail--;
	b = (zp->buffer >> zp->avail) & 1;
	if (zp->avail == 0) {
		zp->buffer = zp_read_byte(zp);
		zp->avail = 8;
	}
	return b;
}

void
zp_init(struct zp_decoder *zp, unsigned char *data, int len)
{
	zp->rp = data;
	zp->ep = data + len;

	zp->a = 0;
	zp->c = zp_read_byte(zp) << 8;
	zp->c |= zp_read_byte(zp);

	zp->buffer = zp_read_byte(zp);
	zp->avail = 8;

	zp->f = zp->c;
	if (zp->f > 0x7fff)
		zp->f = 0x7fff;
}

int
zp_decode(struct zp_decoder *zp, unsigned char *ctx)
{
	unsigned int z, d, b;

	z = zp->a + zp_tab_p[*ctx];

	if (z <= zp->f) {
		zp->a = z;
		return *ctx & 1;
	}

	d = 0x6000 + ((z + zp->a) >> 2);
	if (z > d)
		z = d;

	if (zp->c >= z) {
		b = *ctx & 1;
		if (zp->a >= zp_tab_m[*ctx])
			*ctx = zp_tab_up[*ctx];
		zp->a = z;
	} else {
		b = (*ctx & 1) ^ 1;
		zp->a = zp->a + 0x10000 - z;
		zp->c = zp->c + 0x10000 - z;
		*ctx = zp_tab_dn[*ctx];
	}

	while (zp->a >= 0x8000) {
		zp->a = zp->a + zp->a - 0x10000;
		zp->c = zp->c + zp->c - 0x10000 + zp_read_bit(zp);
	}

	zp->f = zp->c;
	if (zp->f > 0x7fff)
		zp->f = 0x7fff;

	return b;
}

int
zp_decode_pass_through(struct zp_decoder *zp)
{
	unsigned int z, b;

	z = 0x8000 + (zp->a >> 1);

	if (zp->c >= z) {
		b = 0;
		zp->a = z;
	} else {
		b = 1;
		zp->a = zp->a + 0x10000 - z;
		zp->c = zp->c + 0x10000 - z;
	}

	while (zp->a >= 0x8000) {
		zp->a = zp->a + zp->a - 0x10000;
		zp->c = zp->c + zp->c - 0x10000 + zp_read_bit(zp);
	}

	zp->f = zp->c;
	if (zp->f > 0x7fff)
		zp->f = 0x7fff;

	return b;
}
