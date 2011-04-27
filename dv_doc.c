#include "mudjvu.h"

#define TAG(a,b,c,d) (a << 24 | b << 16 | c << 8 | d)

void
usage(void)
{
	fprintf(stderr, "usage: mudjvu input.djvu\n");
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

static unsigned int read32(FILE *f)
{
	unsigned int x = getc(f) << 24;
	x |= getc(f) << 16;
	x |= getc(f) << 8;
	x |= getc(f);
	return x;
}

static unsigned int read24(FILE *f)
{
	unsigned int x = getc(f) << 16;
	x |= getc(f) << 8;
	x |= getc(f);
	return x;
}

static unsigned int read16(FILE *f)
{
	unsigned int x = getc(f) << 8;
	x |= getc(f);
	return x;
}

static int pad16(int n)
{
	return (n + 1) & ~1;
}

static int pad32(int n)
{
	return (n + 3) & ~3;
}

static unsigned int get32(unsigned char *p)
{
	return p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3];
}

static unsigned int get24(unsigned char *p)
{
	return p[0] << 16 | p[1] << 8 | p[2];
}

static unsigned int get16(unsigned char *p)
{
	return p[0] << 8 | p[1];
}

void
dv_parse_dirm(struct dv_document *doc, unsigned char *data, unsigned int len)
{
	unsigned int flags, count, offset, i;
	printf("DIRM {\n");
	flags = data[0];
	count = get16(data + 1);
	if (flags & (1 << 7)) {
		printf("bundled\n");
		for (i = 0; i < count; i++) {
			offset = get16(data + 3 + i * 4);
			printf("offset %d\n", offset);
		}
		offset = 3 + count * 4;
		dv_decode_bzz(data + offset, len - offset);
	} else {
		printf("indirect\n");
		dv_decode_bzz(data + 3, len - 3);
	}
	printf("}\n");
}

void
dv_parse_navm(struct dv_document *doc, unsigned char *data, unsigned int len)
{
	printf("NAVM {\n");
	dv_decode_bzz(data, len);
	printf("}");
}

void
dv_parse_antz(struct dv_document *doc, unsigned char *data, unsigned int len)
{
	printf("ANTz {\n");
	dv_decode_bzz(data, len);
	printf("}");
}

void
dv_parse_txtz(struct dv_document *doc, unsigned char *data, unsigned int len)
{
	printf("TXTz {\n");
	dv_decode_bzz(data, len);
	printf("}");
}

int
dv_read_chunk(struct dv_document *doc, unsigned int tag, unsigned int len)
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
	else
		printf("tag %c%c%c%c\n", tag>>24, tag>>16, tag>>8, tag);
	free(data);
}

int
dv_read_form(struct dv_document *doc, unsigned int tag, unsigned int form_len)
{
	unsigned int len, ofs = 4;
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
		ofs += 8 + pad16(len);
	}
	printf("}\n");
	return 0;
}

int
dv_read_iff(struct dv_document *doc)
{
	unsigned int att, tag, len;

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

