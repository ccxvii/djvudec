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

static int pad16(int n)
{
	return (n + 1) & ~1;
}

static int pad32(int n)
{
	return (n + 3) & ~3;
}

void
dv_parse_dirm(struct dv_document *doc, unsigned char *data, unsigned int len)
{
	printf("parse DIRM tag\n");
}

void
dv_parse_navm(struct dv_document *doc, unsigned char *data, unsigned int len)
{
	printf("parse NAVM tag\n");
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

