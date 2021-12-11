#include <assert.h>
#include <err.h>
#include <malloc.h>
#include <stdarg.h>
#include <stdbool.h>
#include <string.h>

#include "resizable_buf.h"

#ifndef NDEBUG
#define NDEBUG
#endif

// don't make buffer smaller than this when freeing memory.
#define MIN_SIZE 32

extern inline void *rb_ptr(const rb_t *rb);
extern inline void *rb_endptr(const rb_t *rb);
extern inline int rb_len(const rb_t *rb);
extern inline int rb_red_zone(const rb_t *rb);

//extern inline void rb_clear(rb_t *rb);

extern inline int fifo_len(const fifo_t *fifo);
extern inline void *fifo_head(const fifo_t *fifo);
extern inline bool fifo_is_empty(const fifo_t *fifo);
extern inline void fifo_discard(fifo_t *fifo);

void rb_free(rb_t *rb)
{
        free(rb->buf);
        *rb = (rb_t)RB_BLANK;
}

/* ensure there are at least sz bytes past current length */
void
rb_grow(rb_t *rb, size_t sz)
{
        assert(rb->len <= rb->size);
        assert(!rb->size || rb->buf);
        unsigned osz = rb->size;
        while (rb->len + sz > osz)
                osz = osz + (osz >> 1) + 8;
        if (osz != rb->size) {
                rb->size = osz;
                rb->buf = realloc(rb->buf, rb->size);
        }
        assert(rb->len <= rb->size);
        assert(!rb->size || rb->buf);
}

static void
rb_makesize(rb_t *rb, size_t sz, bool preserve)
{
        assert(rb->len <= rb->size);
        assert((rb->buf && rb->size) || (!rb->buf && !rb->size));
        if (sz > rb->size) {
                while (sz > rb->size)
                        rb->size = rb->size + (rb->size >> 1) + 8;
                if (preserve)
                        rb->buf = realloc(rb->buf, rb->size);
                else {
                        free(rb->buf);
                        rb->buf = malloc(rb->size);
                }
        }
        assert(rb->len <= rb->size);
}

void
rb_resize(rb_t *rb, size_t len, bool preserve)
{
        rb_makesize(rb, len, preserve);
        rb->len = len;
        assert(rb->len <= rb->size);
}

void
rb_resize_fill(rb_t *rb, size_t len, char fillvalue)
{
        rb_makesize(rb, len, true);
        if (len > rb->len)
                memset(rb_ptr(rb) + rb->len, fillvalue, len - rb->len);
        rb->len = len;
        assert(rb->len <= rb->size);
}

void *
rb_calloc(rb_t *rb, size_t len)
{
        return memset(rb_push(rb, len), 0, len);
}

/* parameters are reversed from usual to mimic c putc, this allows passing it to
 * functions that expect a putc interface.
 */
int rb_putc(char ch, rb_t *rb)
{
        rb_grow(rb, 1);
        *(char *)(rb->buf + rb->len) = ch;
        rb->len++;
        return ch;
}

/* append a unicode codepoint onto the buffer encoded in utf8. returns the
 * number of bytes appended. */
int rb_putwc(uint32_t wc, rb_t *rb)
{
        rb_grow(rb, 4);
        uint8_t *s = rb_endptr(rb);
        int i = 0;
        if (wc <= 0x7f) {
                s[i++] = (uint8_t)wc;
        } else {
                if (wc <= 0x7ff) {
                        s[i++] = (uint8_t)((wc >> 6) | 0xc0);
                } else {
                        if (wc <= 0xffff) {
                                s[i++] = (uint8_t)((wc >> 12) | 0xe0);
                        } else {
                                s[i++] = (uint8_t)((wc >> 18) | 0xf0);
                                s[i++] = (uint8_t)(((wc >> 12) & 0x3f) | 0x80);
                        }
                        s[i++] = (uint8_t)(((wc >> 6) & 0x3f) | 0x80);
                }
                s[i++] = (uint8_t)((wc & 0x3f) | 0x80);
        }
        rb->len += i;
        return i;
}


void *
rb_append(rb_t *rb, void *data, size_t len)
{
        return memcpy(rb_push(rb, len), data, len);
}

void *
rb_set(rb_t *rb, void *data, size_t len)
{
        rb_makesize(rb, len, false);
        rb->len = len;
        assert(rb->len <= rb->size);
        return memcpy(rb->buf, data, len);
}

char *
rb_strcpy(rb_t *rb, char *str)
{
        rb_set(rb, str, strlen(str));
        return rb_stringize(rb);
}

char *
rb_strcat(rb_t *rb, char *str)
{
        char *res = rb_append(rb, str, strlen(str));
        rb_stringize(rb);
        return res;
}

/* similar to rb_strcat but returns number of bytes copied rather than pointer
 * to destination, follows stdio argument convention */
int
rb_puts(char *str, rb_t *rb)
{
        int len = strlen(str);
        rb_append(rb, str, len);
        rb_stringize(rb);
        return len;
}

/* Copy part of rb2 and append it to rb, returns a pointer into rb's buffer where the
 * extracted data lives. */
void *
rb_extract(rb_t *rb, const rb_t *rb2, unsigned loc, size_t len)
{
        ssize_t nlen = len;
        if (loc + len > rb->len)
                nlen -= loc + len - rb->len;
        if (nlen <  0)
                return rb_endptr(rb);
        return rb_append(rb, (char *)rb_ptr(rb2) + loc, len);
}

/* insert space into a resizable buf moving existing data or extending the size
 * of the buf if you are inserting space past the end. new space will be
 * uninitialized.
 * follow with rb_memset to initialize the space to a value if you require it.
 *
 * one past the end of the buffer is moved as well to preserve stringification.
 */
void *
rb_insert_space(rb_t *rb, unsigned loc, size_t len)
{
        assert(rb->len <= rb->size);
        assert((rb->buf && rb->size) || (!rb->buf && !rb->size));
        if (loc + len > rb->len) {
                /* if we are inserting past the end, no need to memmove any data
                 * around */
                rb_resize(rb, loc + len, true);
        } else {
                rb_grow(rb, len + 1);
                memmove((char *)rb->buf + loc + len, (char *)rb->buf + loc,
                        rb->len - loc + 1);
                rb->len += len;
        }
        assert(rb->len <= rb->size);
        assert((rb->buf && rb->size) || (!rb->buf && !rb->size));
        return rb_ptr(rb) + loc;
}

/* set memory to the specific value, will grow buffer as needed to contain the
 * range specified. loc may be beyond the end of the buffer.
 * returns pointer to the beginning of the changed memory. */
void *
rb_memset(rb_t *rb, char what, unsigned loc, size_t len)
{
        if (loc + len > rb->len)
                rb_resize(rb, loc + len, true);
        return memset(rb_ptr(rb) + loc, what, len);
}


/* insert some data at the given location moving data as needed to make space.
 * if loc is beyond the end of the buffer, uninitialized space will be added
 * to the hole. one past the end of the buffer is also moved to preserve
 * terminators. */
void *
rb_insert(rb_t *rb, unsigned loc, char *data, size_t len)
{
        return memcpy(rb_insert_space(rb, loc, len), data, len);
}

/* delete some data from inside a buffer, moving any trailing data as needed. if
 * loc is greater than the size of the array then nothing happens.
 * when moving data one char past the end is moved to preserve null terminators. */

void
rb_delete(rb_t *rb, unsigned loc, size_t len)
{
        assert(loc <= rb->len);
        assert(rb->len <= rb->size);
        assert((rb->buf && rb->size) || (!rb->buf && !rb->size));
        if (loc + len > rb->len) {
                if (loc < rb->len)
                        rb->len = loc;
        } else {
                rb_grow(rb, 1);
                memmove((char *)rb->buf + loc, (char *)rb->buf + loc + len,
                        rb->len - (loc + len) + 1);
                rb->len -= len;
        }
        assert(rb->len <= rb->size);
        assert((rb->buf && rb->size) || (!rb->buf && !rb->size));
}

/* make sure data is null terminated without actually increasing its length. so
 * you can pass it to routines that expect null terminated data but the length
 * still reflects the length of the data. This is idempotent and will not grow
 * the buffer if it isn't needed. */
char *
rb_stringize(rb_t *rb)
{
        rb_grow(rb, 1);
        ((char *)rb->buf)[rb->len] = '\0';
        return rb_ptr(rb);
}

void *rb_peek(rb_t *rb, int n)
{
        if (n <= rb->len) {
                return rb_endptr(rb) - n;
        } else
                return NULL;
}

void *rb_assign(rb_t *dst, rb_t *src)
{
        rb_free(dst);
        *dst = *src;
        *src = (rb_t)RB_BLANK;
        return rb_ptr(dst);
}

void rb_swap(rb_t *x, rb_t *y)
{
        rb_t t = *x;
        *x = *y;
        *y = t;
}

/* take over ownership of the malloc'ed buffer */
void *rb_take(rb_t *rb)
{
        void *r = rb_ptr(rb);
        *rb = (rb_t)RB_BLANK;
        return r;
}

void *rb_pop(rb_t *rb, int n)
{
        if (n <= rb->len) {
                rb->len -= n;
                return rb_endptr(rb);
        } else
                return NULL;
}
void *rb_push(rb_t *rb, int n)
{
        rb_grow(rb, n);
        void *ret = rb->buf + rb->len;
        rb->len += n;
        return ret;
}

/* the pointer returned becomes invalid after any fifo mutation */
void *
fifo_append(fifo_t *fifo, void *data, size_t len)
{
        if (!(!fifo->offset || len < fifo->rb.size - fifo->rb.len)) {
                // TODO we should grow rather than move up to a certain point.
                memmove(fifo->rb.buf, fifo->rb.buf + fifo->offset, fifo_len(fifo));
                fifo->rb.len -= fifo->offset;
                fifo->offset = 0;
        }
        return rb_append(&fifo->rb, data, len);
}

/* the pointer returned becomes invalid after any fifo mutation */
void *fifo_dequeue(fifo_t *fifo, size_t len)
{
        void *res = fifo_head(fifo);
        if (len > fifo_len(fifo))
                return NULL;
        if (len == fifo_len(fifo))
                fifo_discard(fifo);
        else
                fifo->offset += len;
        return res;
}


/* returns bytes read if successful or -(bytes read + 1) if an error occured. */
/* pass -1 to second argument to read entire file */
#define __MIN(x,y) ((x) < (y) ? (x) : (y))
ssize_t
rb_fread(rb_t *rb, FILE *fh, size_t n)
{
        ssize_t tr = 0;
        while (!feof(fh) && tr <= n) {
                char buf[4096];
                size_t res = fread(buf, 1, __MIN(n - tr, sizeof(buf)), fh);
                rb_append(rb, buf, res);
                tr += res;
                if (ferror(fh)) {
                        tr =  - (tr + 1);
                        break;
                }
        }
        rb_stringize(rb);
        return tr;
}

/* returns bytes read if successful or -(bytes written + 1) if an error occured. */
/* pass -1 to second argument to write entire buffer */
ssize_t
rb_fwrite(rb_t *rb, FILE *fh, size_t n)
{
        if (n > rb->len)
                n = rb->len;
        size_t tw = 0;
        while (n > 0) {
                size_t res = fwrite(rb_ptr(rb) + tw, 1, n, fh);
                tw += res;
                n -= res;
                if (ferror(fh))
                        return -(tw + 1);
        }
        return tw;
}

/* read an entire file into an rb, data is stringized. */
ssize_t
rb_read_file(rb_t *rb, char *fname)
{
        FILE *fh = fopen(fname, "rb");
        if (!fh) {
                warn("rb_read_file(%s)", fname);
                return -1;
        }
        ssize_t res = rb_fread(rb, fh, -1);
        rb_stringize(rb);
        return res;
}


ssize_t
rb_write_file(rb_t *rb, char *fname, bool append)
{
        FILE *fh = fopen(fname, append ? "ab" : "wb");
        if (!fh) {
                warn("rb_write_file(%s)", fname);
                return -1;
        }
        return rb_fwrite(rb, fh, -1);
}

int
rb_printf(rb_t *rb, char *fmt, ...)
{
        va_list ap;
        va_start(ap, fmt);
        int size = vsnprintf(NULL, 0, fmt, ap);
        va_end(ap);
        if (size < 0)
                return size;
        rb_grow(rb, size + 1);
        va_start(ap, fmt);
        size = vsnprintf(rb->buf + rb->len, size + 1, fmt, ap);
        va_end(ap);
        if (size < 0)
                return size;
        rb->len += size;
        return size;
}

// unit tests
#if 0
#ifdef NDEBUG
#undef NDEBUG
#endif

int main(int argc, char *argv[])
{
        rb_t rb = RB_BLANK;
        assert(!rb_ptr(&rb));
        assert(!rb_endptr(&rb));
        assert(!rb_len(&rb));
        rb_printf(&rb, "hello %i %sX", 43, "test");
        assert(*RBP(char, &rb) == 'h');
        assert(*RBPE(char, &rb) == 0);
        assert(rb_len(&rb) == 14);
        /* lets pad out with non nulls */
        rb_grow(&rb, 15);
        memset(RBPE(char, &rb) + 1, 'x', 14);
        rb_insert(&rb, 5, ", world", 7);
        puts(rb_ptr(&rb));
        assert(!strcmp(rb_ptr(&rb), "hello, world 43 testX"));
        rb_delete(&rb, 2, 7);
        puts(rb_ptr(&rb));
        rb_delete(&rb, 10, 2);
        puts(rb_ptr(&rb));
        assert(!strcmp(rb_ptr(&rb), "herld 43 ttX"));
        return 0;
}
#endif
