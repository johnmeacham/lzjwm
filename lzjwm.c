#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <inttypes.h>

/* command line program for testing C implementation. 
 * the lzjwm.py python implementation has
 * more features for encoding. */


#include "resizable_buf.h"
#include "lzjwm.h"

#define nitems(x)       (sizeof((x)) / sizeof((x)[0]))


/*
 * usage, this alwayws works with stdin and stdout
 *
 * -x dump encoded data in text format for debugging
 * -c compress data
 * -d decompress data
 * -v print parameters of encoding
 * -S decompress via the streaming method
 */

#define PI(x) printf("%1$-16s = %2$" PRIiMAX "\n", #x, (intmax_t)(x))

int main(int argc, char *argv[])
{
        rb_t rb = RB_BLANK;
        int opt, mode = 'd';
        while ((opt = getopt(argc, argv, "nvpdcxS")) != -1)
                mode = opt;
        if (mode == 'v') {
                PI(COUNT_BITS);
                PI(MAX_MATCH);
                PI(MAX_ZERO_MATCH);
                PI(LOOKBACK);
                PI(ZERO_BITS);
                exit(0);
        }
        if (rb_fread(&rb, stdin, -1) < 0)
                exit(1);
        switch (mode) {
        case 'x':
                lzjwm_dump(rb_ptr(&rb), rb_len(&rb));
                exit(0);
        case 'S':
                lzjwm_decompress_stream(rb_ptr(&rb), rb_len(&rb), (int (*)(int, void *))fputc, stdout);
                exit(0);
        }
        rb_t rbo = RB_BLANK;
        if (mode == 'd') {
                size_t dsize = lzjwm_decompressed_size(rb_ptr(&rb), rb_len(&rb));
                rb_resize(&rbo, dsize, false);
                if (lzjwm_decompress(rb_ptr(&rb), rb_len(&rb), rb_ptr(&rbo)) < 0)
                        exit(1);
        } else {
                rb_resize(&rbo, rb_len(&rb), false);
                ssize_t nsz = lzjwm_compress(rb_ptr(&rb), rb_len(&rb), rb_ptr(&rbo));
                if (nsz < 0)
                        exit(1);
                rb_resize(&rbo, nsz, true);
        }
        if (mode == 'c')
                fprintf(stderr, "compressing: %li -> %li (%.2f%%)\n", (long)rb_len(&rb), (long)rb_len(&rbo), (1.0 - (float)rb_len(&rbo) / (float)rb_len(&rb)) * 100.0);
        rb_fwrite(&rbo, stdout, -1);
        return 0;
}
