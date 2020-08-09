#include <malloc.h>
#include <assert.h>
#include <string.h>
#include <stdbool.h>
#include <err.h>

#include "resizable_buf.h"

#define NDEBUG

extern inline void *rb_ptr(const rb_t *rb);
extern inline void *rb_endptr(const rb_t *rb);
extern inline unsigned rb_len(const rb_t *rb);
extern inline void rb_clear(rb_t *rb);

void rb_free(rb_t *rb)
{
        free(rb->buf);
        *rb = (rb_t)RB_BLANK;
}

/* ensure there are at least sz bytes past current length */
static void
rb_grow(rb_t *rb, size_t sz)
{
        assert(rb->len <= rb->size);
        assert(!rb->size || rb->buf);
        while (rb->len + sz > rb->size)
                rb->size = rb->size + (rb->size >> 1) + 8;
        rb->buf = realloc(rb->buf, rb->size);
        assert(rb->buf);
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

int rb_putc(char ch, rb_t *rb)
{
        rb_grow(rb, 1);
        *(char *)(rb->buf + rb->len) = ch;
        rb->len++;
        return ch;
}

void
rb_append(rb_t *rb, void *data, size_t len)
{
        rb_grow(rb, len);
        memcpy((char *)rb->buf + rb->len, data, len);
        rb->len += len;
        assert(rb->len <= rb->size);
}

void
rb_set(rb_t *rb, void *data, size_t len)
{
        rb_makesize(rb, len, false);
        memcpy(rb->buf, data, len);
        rb->len = len;
        assert(rb->len <= rb->size);
}

void
rb_strcpy(rb_t *rb, char *str)
{
        rb_set(rb, str, strlen(str));
        rb_stringize(rb);
}

void
rb_strcat(rb_t *rb, char *str)
{
        rb_append(rb, str, strlen(str));
        rb_stringize(rb);
}

void
rb_extract(rb_t *rb, const rb_t *rb2, unsigned loc, size_t len)
{
        rb_set(rb, (char *)rb_ptr(rb2) + loc, len);
}

void
rb_insert_space(rb_t *rb, unsigned loc, size_t len)
{
        assert(rb->len <= rb->size);
        assert((rb->buf && rb->size) || (!rb->buf && !rb->size));
        assert(loc <= rb->len);
        rb_grow(rb, len);
        memmove((char *)rb->buf + loc + len, (char *)rb->buf + loc,
                rb->len - loc);
        rb->len += len;
        assert(rb->len <= rb->size);
        assert((rb->buf && rb->size) || (!rb->buf && !rb->size));
}

void
rb_insert(rb_t *rb, unsigned loc, char *data, size_t len)
{
        rb_insert_space(rb, loc, len);
        memcpy((char *)rb->buf + loc, data, len);
}

void
rb_delete(rb_t *rb, unsigned loc, size_t len)
{
        assert(loc <= rb->len);
        assert(rb->len <= rb->size);
        assert((rb->buf && rb->size) || (!rb->buf && !rb->size));
        memmove((char *)rb->buf + loc, (char *)rb->buf + loc + len,
                rb->len - (loc + len));
        rb->len -= len;
        assert(rb->len <= rb->size);
        assert((rb->buf && rb->size) || (!rb->buf && !rb->size));
}

void
rb_stringize(rb_t *rb)
{
        rb_grow(rb, 1);
        ((char *)rb->buf)[rb->len] = '\0';
}

void *rb_peek(rb_t *rb, int n)
{
        if (n <= rb->len) {
                return rb_endptr(rb) - n;
        } else
                return NULL;
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

void
fifo_append(fifo_t *fifo, void *data, size_t len)
{
        if (!fifo->offset || len < fifo->rb.size - fifo->rb.len) {
                rb_append(&fifo->rb, data, len);
        } else {
                memmove(fifo->rb.buf, fifo->rb.buf + fifo->offset,
                        FIFO_LEN(*fifo));
                fifo->rb.len -= fifo->offset;
                fifo->offset = 0;
                rb_append(&fifo->rb, data, len);
        }
}


/* returns bytes read if successful or -(bytes read + 1) if an error occured. */
/* pass -1 to second argument to read entire file */
#define __MIN(x,y) ((x) < (y) ? (x) : (y))
ssize_t
rb_fread(rb_t *rb, FILE *fh, size_t n)
{
        size_t tr = 0;
        while (!feof(fh)) {
                char buf[4096];
                size_t res = fread(buf, 1, __MIN(n, sizeof(buf)), fh);
                if (!res)
                        return -(tr + 1);
                rb_append(rb, buf, res);
                tr += res;
                if (tr >= n)
                        break;
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
                if (!res && ferror(fh))
                        return -(tw + 1);
                tw += res;
                n -= res;
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
