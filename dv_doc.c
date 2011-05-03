#include "djvudec.h"

#define TAG(a,b,c,d) (a << 24 | b << 16 | c << 8 | d)

struct chunk {
	char *name;
	int offset;
	int size;
	int flag;
};

struct page {
	int chunk;
	int w, h, dpi, gamma, rotate;
	struct jb2library *lib;
	struct jb2image *mask;
};

struct djvu {
	FILE *file;

	int chunk_count;
	int page_count;
	struct chunk *chunk;
	struct page *page;
};

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
dv_read_fgbz(struct djvu *doc, unsigned char *data, int size)
{
	int hascorr;
	int colors, i;

	hascorr = data[0] >> 7;
	colors = get16(data + 1);

	printf("palette (%d) {\n", colors);
	for (i = 0; i < colors; i++)
		printf("\t0x%06x\n", get24(data + 3 + i * 3));
	printf("}\n");

	if (hascorr) {
		int datasize;
		unsigned char *sdata = data + 3 + colors * 3;

		datasize = get24(sdata);
		printf("correspondence (%d) {\n", datasize);
		for (i = 0; i < datasize; i++)
			printf("\t0x%06x\n", get24(sdata + 3 + i * 16));
		printf("}\n");
	}
}

void
dv_read_iw44(struct djvu *doc, unsigned char *data, int size)
{
	printf("BG44 {\n");
	int serial, slices;

	serial = data[0];
	slices = data[1];
	printf("serial = %d\n", serial);
	printf("slices = %d\n", slices);

	if (serial == 0) {
		int comps = data[2] >> 7;
		int version = get16(data + 2) & 0x7fff;
		int width = get16(data + 4);
		int height = get16(data + 6);
		int crdelay = data[8];

		printf("comps = %d\n", comps);
		printf("version = 0x%04x\n", version);
		printf("width = %d\n", width);
		printf("height = %d\n", height);
		printf("crdelay = %d\n", crdelay);
	}
	//iw44_decode_image(data, size);
	printf("}\n");
}

static int djvu_read_chunk(struct djvu *doc, int page);

int
djvu_read_incl(struct djvu *doc, int page, unsigned char *data, int size)
{
	unsigned int save, tag, len;
	int error, i, n;

	for (i = 0; i < doc->chunk_count; i++) {
		n = strlen(doc->chunk[i].name);
		if (n == size && !memcmp(data, doc->chunk[i].name, size))
			break;
	}

	if (i == doc->chunk_count)
		return fz_throw("cannot find referenced chunk");

	printf("INCL '%s' {\n", doc->chunk[i].name);

	save = ftell(doc->file);

	fseek(doc->file, doc->chunk[i].offset, 0);
	error = djvu_read_chunk(doc, page);
	if (error < 0)
		return fz_rethrow(error, "bad chunk in INCL");
	fseek(doc->file, save, 0);

	printf("}\n");
	return size;
}

static int
djvu_read_djbz(struct djvu *doc, int pagenum, unsigned char *data, int size)
{
	struct page *page = doc->page + pagenum;
	printf("loading Djbz chunk\n");
	page->lib = jb2_decode_library(data, size);
	return 0;
}

static int
djvu_read_sjbz(struct djvu *doc, int pagenum, unsigned char *data, int size)
{
	struct page *page = doc->page + pagenum;
	char name[30];
	printf("loading Sjbz chunk\n");
	page->mask = jb2_decode_image(data, size, page->lib);
	sprintf(name, "page%03d_mask.pbm", pagenum + 1);
	jb2_write_pbm(page->mask, name);
	return 0;
}

static int
djvu_read_info(struct djvu *doc, int pagenum, unsigned char *data, int len)
{
	struct page *page = doc->page + pagenum;
	int maj, min;

	if (len < 4)
		return fz_throw("bad INFO chunk size");

	page->w = data[0] << 8 | data[1];
	page->h = data[2] << 8 | data[3];

	if (len >= 9) {
		page->dpi = data[6] | data[7] << 8; /* little endian! */
		page->gamma = data[8];
	} else {
		page->dpi = 300;
		page->gamma = 220;
	}

	if (len >= 10) {
		page->rotate = data[9];
	}

	return len;
}

static int
djvu_read_form(struct djvu *doc, int page, int form_len)
{
	unsigned int tag, len, ofs, error;
	tag = read32(doc->file);
	ofs = 4;
printf("FORM:%c%c%c%c {\n", tag>>24, tag>>16, tag>>8, tag);
	while (ofs < form_len) {
		len = djvu_read_chunk(doc, page);
		if (len < 0)
			return fz_rethrow(error, "bad chunk in page");
		if (len & 1)
			getc(doc->file);
		ofs += 8 + len + (len & 1);
	}
printf("}\n");
	return form_len;
}

static int
djvu_read_chunk(struct djvu *doc, int page)
{
	unsigned int tag, len, error;
	tag = read32(doc->file);
	len = read32(doc->file);
	if (tag == TAG('F','O','R','M')) {
		return djvu_read_form(doc, page, len);
	} else {
		unsigned char *data = malloc(len);
		fread(data, 1, len, doc->file);
		if (tag == TAG('I','N','C','L')) {
			error = djvu_read_incl(doc, page, data, len);
			if (error < 0)
				return fz_rethrow(error, "bad INCL chunk");
		} else if (tag == TAG('I','N','F','O')) {
			djvu_read_info(doc, page, data, len);
		} else if (tag == TAG('D','j','b','z')) {
			djvu_read_djbz(doc, page, data, len);
		} else if (tag == TAG('S','j','b','z')) {
			djvu_read_sjbz(doc, page, data, len);
		} else {
			printf("TAG %c%c%c%c\n", tag>>24, tag>>16, tag>>8, tag);
		}
		return len;
	}
}

int
djvu_read_page(struct djvu *doc, int pagenum)
{
	struct page *page = doc->page + pagenum;
	struct chunk *chunk = doc->chunk + page->chunk;
	fseek(doc->file, chunk->offset, 0);
	return djvu_read_chunk(doc, pagenum);
}

static int
djvu_read_doc_dirm(struct djvu *doc, int len)
{
	int count, offset, flags, i;
	unsigned char *udata, *cdata;
	int usize;

	flags = getc(doc->file);
	count = read16(doc->file);

	if ((flags & 0x80) == 0)
		return fz_throw("indirect DjVu files not supported");

	doc->chunk = malloc(count * sizeof(struct chunk));
	doc->page = malloc(count * sizeof(struct page));
	memset(doc->chunk, 0, count * sizeof(struct chunk));
	memset(doc->page, 0, count * sizeof(struct page));

	doc->chunk_count = count;
	doc->page_count = 0;

	for (i = 0; i < count; i++) {
		doc->chunk[i].offset = read32(doc->file);
		doc->chunk[i].size = 0;
		doc->chunk[i].flag = 0;
		doc->chunk[i].name = 0;
	}

	len -= 3 + count * 4;

	cdata = malloc(len);
	fread(cdata, 1, len, doc->file);
	udata = bzz_decode(cdata, len, &usize);

	offset = count * 4;
	for (i = 0; i < count; i++) {
		doc->chunk[i].size = get24(udata + i * 3);
		doc->chunk[i].flag = udata[count * 3 + i];
		doc->chunk[i].name = strdup((char*)udata + offset);

		offset += strlen((char*)udata + offset) + 1;
		if (doc->chunk[i].flag & 0x80)
			offset += strlen((char*)udata + offset) + 1;
		if (doc->chunk[i].flag & 0x40)
			offset += strlen((char*)udata + offset) + 1;

		if ((doc->chunk[i].flag & 0x3f) == 1) {
			doc->page[doc->page_count].chunk = i;
			doc->page_count++;
		}
	}

	free(cdata);
	free(udata);

	return 0;
}

static int
djvu_read_doc_navm(struct djvu *doc, int len)
{
	return 0;
}

static int
djvu_read_doc_djvm(struct djvu *doc, int form_len)
{
	int tag, len, ofs = 0;
	int error;

	if (ofs < form_len) {
		tag = read32(doc->file);
		len = read32(doc->file);
		if (tag != TAG('D','I','R','M'))
			return fz_throw("no DIRM chunk");
		error = djvu_read_doc_dirm(doc, len);
		if (error)
			return fz_rethrow(error, "bad DIRM chunk");
		ofs = 8 + (len & 1);
	}

	if (ofs < form_len) {
		tag = read32(doc->file);
		len = read32(doc->file);
		if (tag == TAG('N','A','V','M'))
			return djvu_read_doc_navm(doc, len);
		ofs += 8 + (len & 1);
	}

	return 0;
}

static int
djvu_read_doc_djvu(struct djvu *doc, int form_len)
{
	doc->chunk = malloc(sizeof(struct chunk));
	doc->page = malloc(sizeof(struct page));
	memset(doc->chunk, 0, sizeof(struct chunk));
	memset(doc->page, 0, sizeof(struct page));

	doc->chunk[0].name = strdup("DJVU");
	doc->chunk[0].offset = 4;
	doc->chunk[0].size = form_len;
	doc->chunk[0].flag = 1;
	doc->chunk_count = 1;

	doc->page[0].chunk = 0;
	doc->page_count = 1;

	return 0;
}

static int
djvu_read_doc(struct djvu *doc)
{
	int magic, form_tag, form_len, tag;

	magic = read32(doc->file);
	form_tag = read32(doc->file);
	form_len = read32(doc->file);

	if (magic != TAG('A','T','&','T'))
		return fz_throw("not a DjVu file (no AT&T signature)");
	if (form_tag != TAG('F','O','R','M'))
		return fz_throw("not a DjVu file (no FORM chunk)");

	tag = read32(doc->file);
	if (tag == TAG('D','J','V','M'))
		return djvu_read_doc_djvm(doc, form_len - 4);
	if (tag == TAG('D','J','V','U'))
		return djvu_read_doc_djvu(doc, form_len - 4);

	return fz_throw("not a DjVu file (no FORM:DJV? chunk)");
}

struct djvu *
djvu_open_file(char *filename)
{
	struct djvu *doc;
	FILE *file;

	file = fopen(filename, "rb");
	if (!file)
		die("cannot open file");

	doc = malloc(sizeof(struct djvu));
	doc->file = file;
	doc->chunk_count = 0;
	doc->chunk = NULL;
	doc->page_count = 0;
	doc->page = NULL;

	djvu_read_doc(doc);

	return doc;
}

void
djvu_close(struct djvu *doc)
{
	fclose(doc->file);
	free(doc->chunk);
	free(doc->page);
	free(doc);
}

int
main(int argc, char **argv)
{
	struct djvu *doc;
	int i;

	if (argc < 2)
		usage();

	doc = djvu_open_file(argv[1]);

	for (i = 0; i < doc->page_count; i++) {
		printf("page %d: '%s' %d+%d\n", i,
			doc->chunk[doc->page[i].chunk].name,
			doc->chunk[doc->page[i].chunk].offset,
			doc->chunk[doc->page[i].chunk].size);
		djvu_read_page(doc, i);
	}

	djvu_close(doc);

	return 0;
}
