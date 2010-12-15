/*
 * lh5wrapper.c
 */

#include <stdint.h>
#include "ar.h"

int unpackable;            /* global, set in io.c */
ulong compsize, origsize;  /* global */

static uchar buffer[DICSIZ];

void lh5Decode(FILE *inputfile, FILE *outputfile, uint32_t originalsize, uint32_t compressedsize)
{
	int n;

	origsize = originalsize;
	compsize = compressedsize;
	arcfile = inputfile;
	outfile = outputfile;

	make_crctable();
	crc = INIT_CRC;
	decode_start();
	while (origsize != 0)
	{
		n = (uint)((origsize > DICSIZ) ? DICSIZ : origsize);
		decode(n, buffer);
		fwrite_crc(buffer, n, outfile);
		origsize -= n;
	}
}
