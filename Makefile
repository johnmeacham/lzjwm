CFLAGS= -Wall  -g -O 

all: lzjwm tiny_lzjwm

lzjwm: lzjwm.c resizable_buf.c  lzjwm_decompress.c lzjwm_compress.c

clean:
	rm asciic tiny_lzjwm

regress: lzjwm
	python3 util/regress.py

.PHONY: regress test clean
