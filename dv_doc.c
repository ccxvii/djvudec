#include "djvudec.h"

#define TAG(a,b,c,d) (a << 24 | b << 16 | c << 8 | d)

void
usage(void)
{
	fprintf(stderr, "usage: djvudec input.djvu\n");
	exit(1);
}

void
die(char *msg)
{
	fprintf(stderr, "error: %s\n", msg);
	exit(1);
}

int
fz_throw(char *msg)
{
	fprintf(stderr, "+ %s\n", msg);
	return -1;
}

int
fz_rethrow(int code, char *msg)
{
	fprintf(stderr, "| %s\n", msg);
	return code;
}

void
fz_catch(int code, char *msg)
{
	fprintf(stderr, "\\ %s\n", msg);
}

static int read32(FILE *f)
{
	int x = getc(f) << 24;
	x |= getc(f) << 16;
	x |= getc(f) << 8;
	x |= getc(f);
	return x;
}

static int read24(FILE *f)
{
	int x = getc(f) << 16;
	x |= getc(f) << 8;
	x |= getc(f);
	return x;
}

static int read16(FILE *f)
{
	int x = getc(f) << 8;
	x |= getc(f);
	return x;
}

static int get32(unsigned char *p)
{
	return p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3];
}

static int get24(unsigned char *p)
{
	return p[0] << 16 | p[1] << 8 | p[2];
}

static int get16(unsigned char *p)
{
	return p[0] << 8 | p[1];
}

void
dv_parse_dirm(struct dv_document *doc, unsigned char *cdata, int csize)
{
	int flags, count, offset, size, flag, i;
	unsigned char *udata;
	int usize;

	printf("DIRM {\n");
	flags = cdata[0];
	count = get16(cdata + 1);

	if (flags & (1 << 7)) {
		printf("bundled\n");
		for (i = 0; i < count; i++) {
			offset = get32(cdata + 3 + i * 4);
			printf("offset %d = %d\n", i, offset);
		}
		offset = 3 + count * 4;
		udata = bzz_decode(cdata + offset, csize - offset, &usize);
	} else {
		printf("indirect\n");
		udata = bzz_decode(cdata + 3, csize - 3, &usize);
	}

	if (!udata) { printf("bzz decode error\n}\n"); return; }

	offset = count * 4;
	for (i = 0; i < count; i++) {
		size = get24(udata + i * 3);
		flag = udata[count * 3 + i];
		printf("chunk %d: size=%d flag=%x name='%s'\n", i, size, flag, udata + offset);
		offset += strlen((char*)udata + offset) + 1;
		if (flag & 0x80) offset += strlen((char*)udata + offset) + 1;
		if (flag & 0x40) offset += strlen((char*)udata + offset) + 1;
	}

	free(udata);

	printf("}\n");
}

void
dv_parse_navm(struct dv_document *doc, unsigned char *cdata, int csize)
{
	unsigned char *udata;
	int usize;
	printf("NAVM {\n");
	udata = bzz_decode(cdata, csize, &usize);
	if (udata) fwrite(udata, 1, usize, stdout);
	free(udata);
	printf("\n}\n");
}

void
dv_parse_antz(struct dv_document *doc, unsigned char *cdata, int csize)
{
	unsigned char *udata;
	int usize;
	printf("ANTz {\n");
	udata = bzz_decode(cdata, csize, &usize);
	if (udata) fwrite(udata, 1, usize, stdout);
	free(udata);
	printf("\n}\n");
}

void
dv_parse_txtz(struct dv_document *doc, unsigned char *cdata, int csize)
{
	unsigned char *udata;
	int usize;
	printf("TXTz {\n");
	udata = bzz_decode(cdata, csize, &usize);
	if (udata) fwrite(udata, 1, usize, stdout);
	free(udata);
	printf("\n}\n");
}

void
dv_parse_info(struct dv_document *doc, unsigned char *data, int size)
{
	int w, h, min, maj, dpi, gamma, flags;
	w = get16(data);
	h = get16(data + 2);
	min = data[4];
	maj = data[5];
	dpi = data[6] | data[7] << 8; /* wtf, little endian here!? */
	gamma = data[8];
	flags = data[9];
	printf("INFO {\n");
	printf("\t%d x %d\n", w, h);
	printf("\t%d dpi\n", dpi);
	printf("\t%d.%d gamma\n", gamma/10, gamma%10);
	printf("\t%d rotation\n", flags);
	printf("}\n");
}

void
dv_parse_incl(struct dv_document *doc, unsigned char *data, int size)
{
	printf("INCL '");
	fwrite(data, 1, size, stdout);
	printf("'\n");
}

void
dv_parse_djbz(struct dv_document *doc, unsigned char *data, int size)
{
	printf("Djbz {\n");
	doc->dict = jb2_new_decoder(data, size, NULL);
	jb2_decode(doc->dict);
	printf("}\n");
}

void
dv_parse_sjbz(struct dv_document *doc, unsigned char *data, int size)
{
	struct jb2dec *jb;
	printf("Sjbz {\n");
	jb = jb2_new_decoder(data, size, doc->dict);
	jb2_decode(jb);
	jb2_print_page(jb);
	jb2_free_decoder(jb);
	printf("}\n");
exit(0);
}

void
dv_parse_iw44(struct dv_document *doc, unsigned char *data, int size)
{
	printf("BG44 {\n");
	iw44_decode(data, size);
	printf("}\n");
exit(0);
}

void
dv_read_chunk(struct dv_document *doc, unsigned int tag, int len)
{
	unsigned char *data = malloc(len);
	fread(data, 1, len, doc->file);
	if (len & 1) getc(doc->file);
	if (tag == TAG('D','I','R','M'))
		dv_parse_dirm(doc, data, len);
	else if (tag == TAG('N','A','V','M'))
		dv_parse_navm(doc, data, len);
	else if (tag == TAG('A','N','T','z'))
		dv_parse_antz(doc, data, len);
	else if (tag == TAG('T','X','T','z'))
		dv_parse_txtz(doc, data, len);
	else if (tag == TAG('I','N','F','O'))
		dv_parse_info(doc, data, len);
	else if (tag == TAG('I','N','C','L'))
		dv_parse_incl(doc, data, len);
//	else if (tag == TAG('D','j','b','z'))
//		dv_parse_djbz(doc, data, len);
//	else if (tag == TAG('S','j','b','z'))
//		dv_parse_sjbz(doc, data, len);
	else if (tag == TAG('B','G','4','4'))
		dv_parse_iw44(doc, data, len);
	else
		printf("tag %c%c%c%c\n", tag>>24, tag>>16, tag>>8, tag);
	free(data);
}

int
dv_read_form(struct dv_document *doc, int tag, int form_len)
{
	int len, ofs = 4;
	printf("FORM:%c%c%c%c {\n", tag>>24, tag>>16, tag>>8, tag);
	while (ofs < form_len) {
		tag = read32(doc->file);
		len = read32(doc->file);
		if (tag == TAG('F','O','R','M')) {
			tag = read32(doc->file);
			dv_read_form(doc, tag, len - 4);
		} else {
			dv_read_chunk(doc, tag, len);
		}
		ofs += 8 + (len & 1);
	}
	printf("}\n");
	return 0;
}

int
dv_read_iff(struct dv_document *doc)
{
	int att, tag, len;

	att = read32(doc->file);
	tag = read32(doc->file);
	len = read32(doc->file);

	if (att != TAG('A','T','&','T'))
		return fz_throw("not a DjVu file (bad AT&T signature)");
	if (tag != TAG('F','O','R','M'))
		return fz_throw("not a DjVu file (bad FORM chunk)");

	tag = read32(doc->file);

	return dv_read_form(doc, tag, len - 4);
}

struct dv_document *
dv_open_document(FILE *file)
{
	struct dv_document *doc;

	doc = malloc(sizeof(struct dv_document));
	doc->file = file;
	doc->dict = NULL;

	dv_read_iff(doc);

	return doc;
}

int
main(int argc, char **argv)
{
	struct dv_document *doc;
	FILE *file;

	if (argc < 2)
		usage();

	file = fopen(argv[1], "rb");
	if (!file)
		die("cannot open file");

	doc = dv_open_document(file);

	fclose(file);

	return 0;
}

