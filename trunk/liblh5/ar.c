/***********************************************************
	ar.c -- main file
***********************************************************/

static char *usage =
	"ar -- compression archiver -- written by Haruhiko Okumura\n"
	"  PC-VAN:SCIENCE        CompuServe:74050,1022\n"
	"  NIFTY-Serve:PAF01022  INTERNET:74050.1022@compuserve.com\n"
	"Usage: ar command archive [file ...]\n"
	"Commands:\n"
	"   a: Add files to archive (replace if present)\n"
	"   x: Extract files from archive\n"
	"   r: Replace files in archive\n"
	"   d: Delete files from archive\n"
	"   p: Print files on standard output\n"
	"   l: List contents of archive\n"
	"If no files are named, all files in archive are processed,\n"
	"   except for commands 'a' and 'd'.\n"
	"You may copy, distribute, and rewrite this program freely.\n";

/***********************************************************

Structure of archive block (low order byte first):
-----preheader
 1	basic header size
		= 25 + strlen(filename) (= 0 if end of archive)
 1	basic header algebraic sum (mod 256)
-----basic header
 5	method ("-lh0-" = stored, "-lh5-" = compressed)
 4	compressed size (including extended headers)
 4	original size
 4	not used
 1	0x20
 1	0x01
 1	filename length (x)
 x	filename
 2	original file's CRC
 1	0x20
 2	first extended header size (0 if none)
-----first extended header, etc.
-----compressed file

***********************************************************/

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "ar.h"

#define FNAME_MAX (255 - 25) /* max strlen(filename) */
#define namelen  header[19]
#define filename ((char *)&header[20])

int unpackable;            /* global, set in io.c */
ulong compsize, origsize;  /* global */

static uchar buffer[DICSIZ];
static uchar header[255];
static uchar headersize, headersum;
static uint  file_crc;
static char  *temp_name;

static uint ratio(ulong a, ulong b)  /* [(1000a + [b/2]) / b] */
{
	int i;

	for (i = 0; i < 3; i++)
		if (a <= ULONG_MAX / 10) a *= 10;  else b /= 10;
	if ((ulong)(a + (b >> 1)) < a) {  a >>= 1;  b >>= 1;  }
	if (b == 0) return 0;
	return (uint)((a + (b >> 1)) / b);
}

static void put_to_header(int i, int n, ulong x)
{
	while (--n >= 0) {
		header[i++] = (uchar)((uint)x & 0xFF);  x >>= 8;
	}
}

static ulong get_from_header(int i, int n)
{
	ulong s;

	s = 0;
	while (--n >= 0) s = (s << 8) + header[i + n];  /* little endian */
	return s;
}

static uint calc_headersum(void)
{
	int i;
	uint s;

	s = 0;
	for (i = 0; i < headersize; i++) s += header[i];
	return s & 0xFF;
}

static int read_header(void)
{
	headersize = (uchar) fgetc(arcfile);
	if (headersize == 0) return 0;  /* end of archive */
	headersum  = (uchar) fgetc(arcfile);
	fread_crc(header, headersize, arcfile);  /* CRC not used */
	if (calc_headersum() != headersum) error("Header sum error");
	compsize = get_from_header(5, 4);
	origsize = get_from_header(9, 4);
	file_crc = (uint)get_from_header(headersize - 5, 2);
	filename[namelen] = '\0';
	return 1;  /* success */
}

static void write_header(void)
{
	fputc(headersize, outfile);
	/* We've destroyed file_crc by null-terminating filename. */
	put_to_header(headersize - 5, 2, (ulong)file_crc);
	fputc(calc_headersum(), outfile);
	fwrite_crc(header, headersize, outfile);  /* CRC not used */
}

static void skip(void)
{
	fseek(arcfile, compsize, SEEK_CUR);
}

static void copy(void)
{
	uint n;

	write_header();
	while (compsize != 0) {
		n = (uint)((compsize > DICSIZ) ? DICSIZ : compsize);
		if (fread ((char *)buffer, 1, n, arcfile) != n)
			error("Can't read");
		if (fwrite((char *)buffer, 1, n, outfile) != n)
			error("Can't write");
		compsize -= n;
	}
}

static void store(void)
{
	uint n;

	origsize = 0;
	crc = INIT_CRC;
	while ((n = fread((char *)buffer, 1, DICSIZ, infile)) != 0) {
		fwrite_crc(buffer, n, outfile);  origsize += n;
	}
	compsize = origsize;
}

static int add(int replace_flag)
{
	long headerpos, arcpos;
	uint r;

	if ((infile = fopen(filename, "rb")) == NULL) {
		fprintf(stderr, "Can't open %s\n", filename);
		return 0;  /* failure */
	}
	if (replace_flag) {
		printf("Replacing %s ", filename);  skip();
	} else
		printf("Adding %s ", filename);
	headerpos = ftell(outfile);
	namelen = strlen(filename);
	headersize = 25 + namelen;
	memcpy(header, "-lh5-", 5);  /* compress */
	write_header();  /* temporarily */
	arcpos = ftell(outfile);
	origsize = compsize = 0;  unpackable = 0;
	crc = INIT_CRC;  encode();
	if (unpackable) {
		header[3] = '0';  /* store */
		rewind(infile);
		fseek(outfile, arcpos, SEEK_SET);
		store();
	}
	file_crc = crc ^ INIT_CRC;
	fclose(infile);
	put_to_header(5, 4, compsize);
	put_to_header(9, 4, origsize);
	memcpy(header + 13, "\0\0\0\0\x20\x01", 6);
	memcpy(header + headersize - 3, "\x20\0\0", 3);
	fseek(outfile, headerpos, SEEK_SET);
	write_header();  /* true header */
	fseek(outfile, 0L, SEEK_END);
	r = ratio(compsize, origsize);
	printf(" %d.%d%%\n", r / 10, r % 10);
	return 1;  /* success */
}

int get_line(char *s, int n)
{
	int i, c;

	i = 0;
	while ((c = getchar()) != EOF && c != '\n')
		if (i < n) s[i++] = (char)c;
	s[i] = '\0';
	return i;
}

static void extract(int to_file)
{
	int n, method;
	uint ext_headersize;

	if (to_file) {
		while ((outfile = fopen(filename, "wb")) == NULL) {
			fprintf(stderr, "Can't open %s\nNew filename: ", filename);
			if (get_line(filename, FNAME_MAX) == 0) {
				fprintf(stderr, "Not extracted\n");
				skip();  return;
			}
			namelen = strlen(filename);
		}
		printf("Extracting %s ", filename);
	} else {
		outfile = stdout;
		printf("===== %s =====\n", filename);
	}
	crc = INIT_CRC;
	method = header[3];  header[3] = ' ';
	if (! strchr("045", method) || memcmp("-lh -", header, 5)) {
		fprintf(stderr, "Unknown method: %u\n", method);
		skip();
	} else {
		ext_headersize = (uint)get_from_header(headersize - 2, 2);
		while (ext_headersize != 0) {
			fprintf(stderr, "There's an extended header of size %u.\n",
				ext_headersize);
			compsize -= ext_headersize;
			if (fseek(arcfile, ext_headersize - 2, SEEK_CUR))
				error("Can't read");
			ext_headersize = fgetc(arcfile);
			ext_headersize += (uint)fgetc(arcfile) << 8;
		}
		crc = INIT_CRC;
		if (method != '0') decode_start();
		while (origsize != 0) {
			n = (uint)((origsize > DICSIZ) ? DICSIZ : origsize);
			if (method != '0') decode(n, buffer);
			else if (fread((char *)buffer, 1, n, arcfile) != n)
				error("Can't read");
			fwrite_crc(buffer, n, outfile);
			if (outfile != stdout) putc('.', stderr);
			origsize -= n;
		}
	}
	if (to_file) fclose(outfile);  else outfile = NULL;
	printf("\n");
	if ((crc ^ INIT_CRC) != file_crc)
		fprintf(stderr, "CRC error\n");
}

static void list_start(void)
{
	printf("Filename         Original Compressed Ratio CRC Method\n");
}

static void list(void)
{
	uint r;

	printf("%-14s", filename);
	if (namelen > 14) printf("\n              ");
	r = ratio(compsize, origsize);
	printf(" %10lu %10lu %u.%03u %04X %5.5s\n",
		origsize, compsize, r / 1000, r % 1000, file_crc, header);
}

static int match(char *s1, char *s2)
{
	for ( ; ; ) {
		while (*s2 == '*' || *s2 == '?') {
			if (*s2++ == '*')
				while (*s1 && *s1 != *s2) s1++;
			else if (*s1 == 0)
				return 0;
			else s1++;
		}
		if (*s1 != *s2) return 0;
		if (*s1 == 0  ) return 1;
		s1++;  s2++;
	}
}

static int search(int argc, char *argv[])
{
	int i;

	if (argc == 3) return 1;
	for (i = 3; i < argc; i++)
		if (match(filename, argv[i])) return 1;
	return 0;
}

static void exitfunc(void)
{
	fclose(outfile);  remove(temp_name);
}

int main(int argc, char *argv[])
{
	int i, j, cmd, count, nfiles, found, done;

	/* Check command line arguments. */
	if (argc < 3
	 || argv[1][1] != '\0'
	 || ! strchr("AXRDPL", cmd = toupper(argv[1][0]))
	 || (argc == 3 && strchr("AD", cmd)))
		error(usage);

	/* Wildcards used? */
	for (i = 3; i < argc; i++)
		if (strpbrk(argv[i], "*?")) break;
	if (cmd == 'A' && i < argc)
		error("Filenames may not contain '*' and '?'");
	if (i < argc) nfiles = -1;  /* contains wildcards */
	else nfiles = argc - 3;     /* number of files to process */

	/* Open archive. */
	arcfile = fopen(argv[2], "rb");
	if (arcfile == NULL && cmd != 'A')
		error("Can't open archive '%s'", argv[2]);

	/* Open temporary file. */
	if (strchr("ARD", cmd)) {
		temp_name = tmpnam(NULL);
		outfile = fopen(temp_name, "wb");
		if (outfile == NULL)
			error("Can't open temporary file");
		atexit(exitfunc);
	} else temp_name = NULL;

	make_crctable();  count = done = 0;

	if (cmd == 'A') {
		for (i = 3; i < argc; i++) {
			for (j = 3; j < i; j++)
				if (strcmp(argv[j], argv[i]) == 0) break;
			if (j == i) {
				strcpy(filename, argv[i]);
				if (add(0)) count++;  else argv[i][0] = 0;
			} else nfiles--;
		}
		if (count == 0 || arcfile == NULL) done = 1;
	}

	while (! done && read_header()) {
		found = search(argc, argv);
		switch (cmd) {
		case 'R':
			if (found) {
				if (add(1)) count++;  else copy();
			} else copy();
			break;
		case 'A':  case 'D':
			if (found) {
				count += (cmd == 'D');  skip();
			} else copy();
			break;
		case 'X':  case 'P':
			if (found) {
				extract(cmd == 'X');
				if (++count == nfiles) done = 1;
			} else skip();
			break;
		case 'L':
			if (found) {
				if (count == 0) list_start();
				list();
				if (++count == nfiles) done = 1;
			}
			skip();  break;
		}
	}

	if (temp_name != NULL && count != 0) {
		fputc(0, outfile);  /* end of archive */
		if (ferror(outfile) || fclose(outfile) == EOF)
			error("Can't write");
		remove(argv[2]);  rename(temp_name, argv[2]);
	}

	printf("  %d files\n", count);
	return EXIT_SUCCESS;
}
