/* This contains fairly complicated versions of the decoder used for debugging
 * and experimenting with new features. if you don't need these features then
 * you can copy the decoder from tiny_lzjwm.c directly into your codebase. */

#include "lzjwm.h"
#include <stdint.h>
#include <stdio.h>

// deconstruct the byte codes. these include a special case for ZERO_BITS that
// is generally not needed but may be useful for specific circumstances.
static uint8_t count(uint8_t c)
{
        if (!(c & 0x80))
                return 1;
        if (ZERO_BITS && (c | ((1 << (COUNT_BITS + ZERO_BITS)) - 1)) == 0xff)
                return (c & ((1 << (COUNT_BITS + ZERO_BITS)) - 1)) + 2;
        else
                return COUNT(c);
}

static uint8_t get_offset(uint8_t c)
{
        if (ZERO_BITS && (c | ((1 << (COUNT_BITS + ZERO_BITS)) - 1)) == 0xff)
                return 0;
        return OFFSET(c) + (ZERO_BITS ? 1 : 0);
}

size_t lzjwm_decompressed_size(const char *in, ssize_t isize)      
{
        size_t size = 0;
        for (int i = 0; (isize == -1 && in[i]) ||  i < isize; i++)
                size += count(in[i]);
        return size;
}

// we pass in the whole buffer in addtion to the current location and how many
// bytes we wish to output. isize == -1 means in is null terminated and howmany
// == -1 means no limit on number of items output.
//
// this requires no buffers whatsoever.

/* simple structure that contains constant data for decompression so we don't
 * keep passing the same thing to recursive calls. */
struct decompress_data {
        const char *input;
        unsigned input_size;
        int (*fputc)(int, void *);
        void *user;
};

static int _decompress_stream(const struct decompress_data *data,
                              unsigned iptr,    // current position
                              unsigned howmany // total needed
                             )
{
        unsigned needed = howmany;
        while (needed && iptr < data->input_size) {
                char ch = data->input[iptr++];
                int len = count(ch);
                if (len == 1) {
                        data->fputc(ch, data->user);
                        needed--;
                } else {
                        int offset = get_offset(ch);
                        if (needed > len)
                                needed -= _decompress_stream(data, iptr - offset - 2, len);
                        else
                                iptr = iptr - offset - 2;
                }
        }
        return howmany - needed;
}


// this calls fn(c,data) for each decoded character in the stream. isize should
// be the size of the input, or -1 if the input is null terminated.
size_t lzjwm_decompress_stream(const char *in, ssize_t isize, int (*fputc)(int c, void *data), void *user)
{
        struct decompress_data data = { .input = in, .input_size = isize, .fputc = fputc, .user = user };
        return _decompress_stream(&data, 0, -1);
}

// non streaming decompression that uses a buffer. this will be
// faster but needs to keep the output available.
//
// decompress some data, assumes you have enough space on out,
// use lzjwm_decompressed_size to find out how much to allocate if
// you do not already know it.
//
// if isize is -1,then the input is assumed to be null terminated.
size_t lzjwm_decompress(const char *in, ssize_t isize,  char *out)
{
        int fsize = 0;
        int iptr = 0;
        while (iptr < (unsigned)isize) {
                char ch = in[iptr++];
                if (!(~isize || ch))
                        break;
                int len = count(ch);
                if (len == 1) {
                        out[fsize] = ch;
                } else {
                        int offset = get_offset(ch);
                        const char *in_finger = in + iptr - 1;
                        int outf = fsize;
                        for (int i = 0; i <= offset; i++)
                                outf -= count(*--in_finger);
                        for (int i = 0; i < len; i++)
                                out[fsize + i] = out[outf + i];
                }
                fsize += len;
        }
        return fsize;
}


/* dump representation of encoded form to  stdout */
void lzjwm_dump(char *in, size_t isize)
{
        for (int i = 0; i < isize; i++) {
                if (in[i] & 0x80) {
                        printf("(%i,%i)", get_offset(in[i]), count(in[i]));
                } else {
                        printf("%c", in[i]);
                }
        }
}

