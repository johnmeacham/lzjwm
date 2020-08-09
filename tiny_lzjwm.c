#include <stdio.h>
#include <stdlib.h>

/* absolute minimal lzjwm decoder */

#define COUNT_BITS 2

#define COUNT(x) (((x) & ((1 << COUNT_BITS) - 1)) + 2)
#define OFFSET(x) (((x) & 0x7f) >> COUNT_BITS)

// print characters from compressed 'data' starting at 'location'
// stopping on a null character or after count characters have been printed.
//
// format
// 0xxxxxxx - literal character xxxxxxx
// 1ooooocc - copy cc + 2 characters from encoded data offset ooooo

void lzjwm_decompress(char *data, unsigned location, unsigned count)
{
        while (count && data[location]) {
                char ch = data[location++];
                if (!(ch & 0x80)) {
                        putchar(ch);
                        count--;
                } else {
                        int nloc = location - OFFSET(ch) - 2;
                        int len = COUNT(ch);
                        if (count > len) {
                                lzjwm_decompress(data, nloc, len);
                                count -= len;
                        } else
                                location = nloc;
                }
        }
}


#if 1



int main(int argc, char *argv[])
{
        size_t bsz = 0, sz = 0;
        char *data = NULL;
        while (!feof(stdin) && !ferror(stdin)) {
                if (bsz - sz < 256) {
                        bsz += 4096;
                        data = realloc(data, bsz);
                }
                sz += fread(data + sz, 1, bsz - sz - 1, stdin);
        }
        data[sz] = '\0';
        lzjwm_decompress(data, 0, -1);
        return 0;
}

#endif
