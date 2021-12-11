#ifndef RESIZABLE_BUF_H
#define RESIZABLE_BUF_H

/* simple automatically resizing buffers and a fifo implementation.
 * these grow as needed with sublinear cost with minimal overhead. */

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <inttypes.h>

typedef struct rb rb_t;

struct rb {
        void *buf;
        int len;
        int size;
};

/* This should be used to initialize new buffers.
 * rb_t rb = RB_BLANK;
 * zero initialized memory also works.
 */
#define RB_BLANK        {NULL, 0, 0}

/*
 * The macro versions accept a type as an argument and cast results
 * to it properly and work on units of that type.
 * The functional versions all work on bytes.
 *
 * The macro versions can also be used as lvalues for setting entries.
 *
 * example:
 * int *iptr = RBP(int, &rb)
 *
 * */

#define RBP(t,rb)          ((t *)((rb)->buf))
#define RBPE(t,rb)         ((t *)((rb)->buf + (rb)->len))
#define RB_NITEMS(t,rb)    ((rb)->len/sizeof(t))
#define RB_LAST(t,rb)      ((t *)((rb)->buf + (rb)->len - sizeof(t)))
#define RB_FIRST(t, b)     (RBP(t,b))

/* stack operations, these may be used directly as values or assigned to.
 *
 * int value = RB_PEEK(int, &rb);
 * RB_PUSH(long long, &rb) = 389;
 *
 */
#define RB_PUSHN(t,rb,n)   (t *)(rb_push(rb, (n)*sizeof(t)))
#define RB_POPN(t,rb,n)    (t *)(rb_pop(rb, (n)*sizeof(t)))
#define RB_PEEKN(t,rb,n)   (t *)(rb_pop(rb, (n)*sizeof(t)))

#define RB_PEEK(t,rb)      *RB_PEEKN(t,rb,1)
#define RB_POP(t,rb)       *RB_POPN(t,rb,1)
#define RB_PUSH(t,rb)      *RB_PUSHN(t,rb,1)


// these require the argument be addressable but don't need the type explicitly
// passed.
#define RB_LPUSH(rb, x)      rb_append(rb, &(x), sizeof(x))
#define RB_LPUSHN(rb, x, n)  rb_append(rb, x, sizeof(*(x))*(n))

#define RB_LPOP(v, rb)       memcpy(&(v), rb_pop(rb, sizeof(v)))
#define RB_LPOPN(v, rb, n)   memcpy(v, rb_pop(rb, (n)*sizeof(*(v))))
#define RB_LPEEK(v, rb)      memcpy(&(v), rb_peek(rb, sizeof(v)))
#define RB_LPEEKN(v, rb, n)  memcpy(v, rb_peek(rb, (n)*sizeof(*(v))))

// these assume v is a ptr and assign it to directly point at the value in the
// rb
#define RB_APOPN(v, rb, n)   do { v = rb_pop(rb, (n)*sizeof(*(v))); } while (0)
#define RB_APEEKN(v, rb, n)  do { v = rb_peek(rb, (n)*sizeof(*(v))); } while (0)

/* sort and search */
typedef int (*rb_cmpfunc)(const void *, const void *);
#define RB_QSORT(t,rb,cfunc) qsort(rb_ptr(rb), RB_NITEMS(rb), (rb_cmpfunc)(cfunc))
#define RB_BSEARCH(t,k,rb,cfunc) bsearch(k, rb_ptr(rb), RB_NITEMS(rb), (rb_cmpfunc)(cfunc))

/*
 * for loop over rb values.
 *
 * unsigned sum = 0;
 * RB_FOR(unsigned, ptr, &rb) {
 *     sum += *ptr;
 * }
 */
#define RB_FOR(t,v,rb)  for (t *v = rb_ptr(rb); (void *)v < rb_endptr(rb); v++)

/* this iterates over pairs of index,value so you can get at the index too. This
 * also allows for the underlying buffer to be mutated. */
#define RB_FOR_ENUM(t,var,rb)  for (struct { int k; t *v; } var = { .k = 0, .v = RBP(t,rb) }; var.k < (int)RB_NITEMS(t, rb); var.k++, var.v = RBP(t,rb) + var.k)

inline void *rb_ptr(const rb_t *rb)
{
        return rb->buf;
}
inline void *rb_endptr(const rb_t *rb)
{
        return rb->buf + rb->len;
}
inline int rb_len(const rb_t *rb)
{
        return rb->len;
}
/* how much space we can safely use past the end of the buffer */
inline int rb_red_zone(const rb_t *rb)
{
        return rb->size - rb->len;
}

/* clears rb to the empty buffer and frees all memory associated with it. */
void rb_free(rb_t *rb);

/* ensure there is a nul padding byte after the end of the data without
 * increasing its length. useful for passing data to routines that expect nul
 * terminated strings. */
char *rb_stringize(rb_t *rb);

/* append data to a buffer, return the location of the appended data */
void *rb_append(rb_t *rb, void *data, size_t len);

/* extract data from one buffer and apend it to another, returns the address of
 * the newly extracted data. will copy what it can if an invalid range is
 * selected. If you need to know how many bytes were copied, check rb_len on the
 * target buffer. */
void *rb_extract(rb_t *rb, const rb_t *rb2, unsigned loc, size_t len);
void *rb_insert(rb_t *rb, unsigned loc, char *data, size_t len);
void *rb_insert_space(rb_t *rb, unsigned loc, size_t len);
void *rb_memset(rb_t *rb, char what, unsigned loc, size_t len);
void *rb_peek(rb_t *rb, int n);
void *rb_pop(rb_t *rb, int n);
void *rb_push(rb_t *rb, int n);
void *rb_set(rb_t *rb, void *data, size_t len);
void rb_delete(rb_t *rb, unsigned loc, size_t len);
void rb_resize(rb_t *rb, size_t len, bool preserve);
void rb_resize_fill(rb_t *rb, size_t len, char fillvalue);
void *rb_calloc(rb_t *rb, size_t len);

/* do not change length but make sure there are at least sz bytes in the redzone
 * */
void rb_grow(rb_t *rb, size_t sz);

/* Do a zero copy transfer of data from one rb to another handling freeing
 * memory properly.
 *
 * after this operation src will be a freed empty buffer, anything previously in
 * dst will have been freed and dst wil contain what used to be in src.
 *
 * returns a pointer to the assigned data.
 * */
void *rb_assign(rb_t *dst, rb_t *src);

/* swap the data between two buffers in place. */
void rb_swap(rb_t *x, rb_t *y);

/* take ownership of the rb buffer, this will return the malloc allocated buffer
 * of rb and clear rb. It is the users responsibilty to free() the buffer when
 * done. */
void *rb_take(rb_t *rb);

/* file operations.
 * These return the number of bytes copied or -(bytes copied + 1) on error. */
ssize_t rb_read_file(rb_t *rb, char *fname);
ssize_t rb_write_file(rb_t *rb, char *fname, bool append);

/* you can pass in -1 to the length to read and write the entire amount of data
 * available */
ssize_t rb_fwrite(rb_t *rb, FILE *fh, size_t n);
ssize_t rb_fread(rb_t *rb, FILE *fh, size_t n);

/* arguments reversed to match putc */
int rb_putc(char ch, rb_t *rb);
int rb_puts(char *str, rb_t *rb);
/* assumes unicode codepoints and utf8 encoding. */
int rb_putwc(uint32_t, rb_t *rb);

/* string operations, these operate with null terminated strings rather than
 * length delimited data. they always leve the data in the buffer null
 * terminated and return the number of characters operated on. the null
 * terminator is not counted as part of the length of the buffer just
 * like stringize */
char *rb_strcpy(rb_t *rb, char *str);
char *rb_strcat(rb_t *rb, char *str);
int rb_printf(rb_t *rb, char *fmt, ...);

/* specialty routines */

/* extended routines, these have a callback that will be called if the
 * underlying memory is reallocated to a new region */
void *rb_appendx(rb_t *rb, void *data, size_t len, void (*)(void *, void *));
void *rb_callocx(rb_t *rb, size_t len, void (*)(void *, void *));


/* fifo are a first in first out buffer. these reset storage when the fifo is
 * empty, so only use when the fifo is periodically emptied.
 * they are just a resizable buffer with an offset into it. */

typedef struct fifo fifo_t;

struct fifo {
        struct rb rb;
        unsigned offset;
};

#define FIFO_BLANK  {RB_BLANK, 0}

inline int fifo_len(const fifo_t *fifo)
{
        return fifo->rb.len - fifo->offset;
}
inline void *fifo_head(const fifo_t *fifo)
{
        return rb_ptr(&fifo->rb) + fifo->offset;
}
inline bool fifo_is_empty(const fifo_t *fifo)
{
        return !fifo_len(fifo);
}

/* discard any pending data. this does not free memory. */
inline void fifo_discard(fifo_t *fifo)
{
        fifo->rb.len = 0;
        fifo->offset = 0;
}
inline void fifo_free(fifo_t *fifo)
{
        rb_free(&fifo->rb);
        fifo->offset = 0;
}

// #define FIFO_POP(f, l) do { if (l == FIFO_LEN(f)) {(f).offset = 0; RB_CLEAR((f).rb); } else (f).offset += l;  } while (0)

void *fifo_append(fifo_t *fifo, void *data, size_t len);
void *fifo_dequeue(fifo_t *fifo, size_t len);

#endif
