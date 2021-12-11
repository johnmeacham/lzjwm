CFLAGS= -Wall  -g -Os

all: lzjwm tiny_lzjwm

tiny_lzjwm: tiny_lzjwm.c
lzjwm: lzjwm.c resizable_buf.c  lzjwm_decompress.c lzjwm_compress.c lzjwm.h resizable_buf.h


clean:
	rm -f -- lzjwm tiny_lzjwm *.o

regress: lzjwm tiny_lzjwm lzjwm.py
	mkdir -p regress/out
	python3 util/regress.py

.PHONY: regress test clean
