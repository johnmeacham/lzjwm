#ifndef LZJWM_H
#define LZJWM_H
/*
 * very simple ascii LZ style compression.
 *
 * if all you want in the minimal decoder copy it from tiny_lzjwm.c directly
 * into your code.
 *
 * all 7 bit data can be encoded as itself, plain ascii can be decoded to
 * itself. null may be used as terminator.
 * it will never increase the size.
 * allows random access to compressed data
 *
 * http://github.com/johnmeacham/lzjwm
 *
 * 5 bits for offset and 2 bits for length
 *
 * so a byte may encode
 * a single character
 * 2-5 characters copied from the last 28 positions
 *
 * how many bits are used for count and offset can be adjusted with COUNT_BITS. 
 *
 * top bit zero means it is a literal byte. just output it.
 * 0ccccccc just the character ccccccc
 * 1xxxxxyy (look back xxxxx + 1 bytes and copy yy + 2 characters)
 */

#include<stdlib.h>

/* steal ZERO_BITS bits for COUNT_BITS for special encoding of offset = 0*/
#define ZERO_BITS 0
#define COUNT_BITS 2 

#define LOOKBACK ((1 << (7 - COUNT_BITS)) - (ZERO_BITS ? (1 << ZERO_BITS) : 0))
#define MAX_MATCH ((1 << COUNT_BITS) + 1)
#define MAX_ZERO_MATCH ((1 << (COUNT_BITS + ZERO_BITS)) + 1)

#define COUNT(x) (((x) & ((1 << COUNT_BITS) - 1)) + 2)
#define OFFSET(x) (((x) & 0x7f) >> COUNT_BITS)


/* get size of uncompressed data given a compressed data block. 
 * isize should be size of data or if -1 is passed in it will
 * assume the data is null terminated */
size_t lzjwm_decompressed_size(const char *in, ssize_t isize);

/* this calls putc(character,user) for each decoded character in the stream. 
 * isize should  be the size of the input, or -1 if the input is null terminated. 
 * returns the number of characters decoded. */
size_t lzjwm_decompress_stream(const char *in, ssize_t isize, int (*putc)(int c, void *data), void *user);

/* decompress into a static buffer. out must have enough space, call
 * lzjwm_decompressed_size to get the size of buffer needed if you don't know it
 * this will be faster than the streaming version as it can use the outgoing
 * buffer as a cache. */
size_t lzjwm_decompress(const char *in, ssize_t isize,  char *out);

/* compress data.
 * see the python version for more features.
 * out must be as big as in, returns a negative number on error */
ssize_t lzjwm_compress(const char *in, size_t isize, char *out);

/* dump representation of encoded stream for debugging */
void lzjwm_dump(char *in, size_t isize);

#endif
