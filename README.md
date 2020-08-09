 LZJWM compression algorithm
 ===========================
 
 This is a varient of (length,distance) style LZ compression that has some
 tweaks that make it optimal for compressing ASCII that can be accessed via
 random access with an extremely tiny decompressor with no ram buffers. In
 fact a complete decompressor follows
 
 
        // print characters from compressed 'data' starting at 'location'
        // stopping on a null character or after count characters have been printed.
        //
        // 0xxxxxxx - literal character xxxxxxx
        // 1ooooocc - copy cc + 2 characters from encoded data offset ooooo
        
        #define COUNT_BITS 2

        #define COUNT(x) (((x) & ((1 << COUNT_BITS) - 1)) + 2)
        #define OFFSET(x) (((x) & 0x7f) >> COUNT_BITS)
 
        void decompress(char *data, unsigned location, unsigned count)
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
                                        decompress(data, nloc, len);
                                        count -= len;
                                } else
                                        location = nloc;
                        }
                }
        }
        
from this a few properties are obvious:

 - It can handle 7 bit data, an ascii string is a valid encoded string.
 - it requires no RAM decoding buffers at all. 
 - It can decompress starting at an arbitrary point. 
 - Although there is a recursive call, it can be statically shown it will
   never call more than 4 deep for any data so it's RAM usage is fixed and
   small. 
 - compiles to less than 80 avr instructions.
 - COUNT_BITS can be modified freely to suit your data.
   
Motivation and Design
---------------------

The main motivation was that I needed a way to include static strings in
embedded code for a serial UI. these consisted of small text prompts as well
as ascii art menus. The uC had only a few kb of ram so could not afford any
buffers for a traditional decompressor. additionally, since many of the
strings were short, they would not compress well to begin with since there
is not enough time to build up a dictionary. The random access nature of
LZJWM means I only need to keep pointers to the beginning of the strings in
the compressed block to immediately print them. 

The fundamental difference from LZ77/78 and LZSS that makes these properties
possible is that rather than the (length,distance) match being in
uncompressed data space, the offset refers to an offset in the compressed
data block. This means that you don't need to keep a buffer of uncompressed
data and can directly refer to previous compressed data when encountering a
match. Additionally the "reach" of the offsets are greatly multiplied,
although 5 bits should only let you look back 32 characters, since the
offset is counting data after it has been compressed, you effectively can
look back hundreds of characters in the uncompressed stream. 

So how well does it compress?
-----------------------------

It does pretty well for how simple it is, and is especially good for its intended
use. 

 -  /usr/share/dict/words is reduced by 61%
 -  program source code is generaly reduced by 40%
 -  ascii art can be reduced by 70%
 -  the calgary corpus is reduced by about 30%

 

 
lzjwm.py encoder
----------------

The python encoder is fairly advanced, it can output the raw data, or c code
that may be included in a project. It can also output everything in yaml
format for you to easily import it into other tools for processing into
other languages.

As input it can take complete files, a file with a list of lines each of
which should be individually addressable, or a yaml file describing the
strings you wish to encode.
 
 
    usage: lzjwm.py [-h] [-c] [-d] [-y] [-z] [-0] [--verbose] [-l] [-s]
                    [-f {raw,c,yaml,c_avr}] [-o O]
                    [file [file ...]]
    optional arguments:
    -h, --help            show this help message and exit
    -c                    compress
    -d                    decompress
    -y                    yaml input
    -z                    never compress null so it appears unchanged in
                            compressed data. useful for random access.
    -0                    append a null terminator to each thing compressed.
    --verbose, -v
    -l                    treat each line in input as its own record
    -s                    attempt to rearange and unify records for better
                            compression
    -f {raw,c,yaml,c_avr}
                            output format when compressing
    -o O                  output file
 
for example if you were to process the following yaml file with 

    ./lzjwm.py -y example.yaml -f c
 
        # example yaml input for lzjwm.py
        - name: bar
        data: 'foobar'
        - name: baz
        data: 'foobaz'
        - name: intro
        data: 'Hello World!'
        - name: outro
        data: 'Goodbye World!'
        
you would get the following, with the offset and lengths what you want to
pass to your decompression routine to get each string back. notice that it
is able to compress foobar and foobaz together even though they are separate
strings.

        #ifndef LZJWM_DATA_H
        #define LZJWM_DATA_H

        #define OFFSET_BAR 0
        #define LENGTH_BAR 6

        #define OFFSET_BAZ 6
        #define LENGTH_BAZ 6

        #define OFFSET_INTRO 9
        #define LENGTH_INTRO 12

        #define OFFSET_OUTRO 21
        #define LENGTH_OUTRO 14

        static const char lzjwm_data[] = 
                "foobarf\226zHello World!G\320dbye\263\240";

        #endif

