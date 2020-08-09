/* simple encoder in C, the python lzjwm.py is more featureful. */

#include "lzjwm.h"
#include <stdint.h>
#include <assert.h>

#define NDEBUG

static char mk_ptr(uint8_t count, uint8_t offset)
{
        assert(count >= 2);
        assert(offset >= 0);
        assert(offset < LOOKBACK);
        if (ZERO_BITS && offset == 0)
                //return 0xf0 | (count - 2);
                return ~((1 << (COUNT_BITS + ZERO_BITS)) - 1)  | (count - 2);
        return 0x80 | ((offset - (ZERO_BITS ? 1 : 0)) << COUNT_BITS) | (count - 2);
}



static int match(const char *data, int x, int y, int max, int max_match)
{
        assert(x != y);
        assert(x <= max);
        assert(y <= max);
        int result = 0;
        int mresult = x > y ? max - x : max - y;
        if (mresult > max_match)
                mresult = max_match;
        while (result < mresult && data[x + result] == data[y + result])
                result++;
        return result;
}

/* out must be at lesat as big as in. */
ssize_t lzjwm_compress(const char *in, size_t isize, char *out)
{
        struct {
                int next, from;
                uint8_t count;
        } *as;
        as = malloc(isize * sizeof(*as));
        for (int i = 0; i < isize; i++) {
                if (in[i] & 0x80) {
                        free(as);
                        return -1;
                }
                as[i].next = i + 1;
                as[i].count = 1;
                as[i].from = 0;
        }
        as[isize - 1].next = -1;
        for (int dptr = 0; dptr != -1; dptr = as[dptr].next) {
                int cl = dptr;
                for (int i = 0; i < LOOKBACK; i++) {
                        cl = as[cl].next;
                        if (cl < 0)
                                break;
                        int m = match(in, dptr, cl, isize, i ? MAX_MATCH : MAX_ZERO_MATCH);
                        if (m >= 2) {
                                int nn = cl;
                                int d = 0, j = 0;
                                for (; nn != -1; d++) {
                                        int c = as[nn].count;
                                        if (j + c > m)
                                                break;
                                        j += c;
                                        nn = as[nn].next;
                                }
                                if (d >= 2) {
                                        as[cl].next = nn;
                                        as[cl].count = j;
                                        as[cl].from = dptr;
                                }
                        }
                }
        }
        int optr = 0;
        for (int i = 0; i != -1; i = as[i].next) {
                if (as[i].count < 2)
                        out[optr] = in[i] & 0x7f;
                else {
                        out[optr] = mk_ptr(as[i].count, optr - as[as[i].from].from - 1);
                }
                as[i].from = optr++;
        }
        free(as);
        return optr;
}
