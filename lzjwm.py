#!/usr/bin/env python3

# The python version is mainly for compressing data to be decompressed
# by the C version. but both contain implementations of compression and
# decompresion.

import string
import sys
import yaml
import io


class Config:
    """ configuration for lzjwm compressor """

    def __init__(self, count_bits=2, zero_bits=0):
        self.count_bits = count_bits
        self.zero_bits = zero_bits
        self.offset_bits = 7 - count_bits
        self.max_offset = 2**(self.offset_bits)
        self.max_match = 2**count_bits + 1
        self.max_zero_match = 2**(count_bits + zero_bits) + 1
        self.no_compress = b''
        if zero_bits:
            self.max_offset -= 2**zero_bits
        self.zero_offset = 1 if zero_bits else 0

    def max_match_for_offset(self, offset):
        if offset == 0:
            return self.max_zero_match
        else:
            return self.max_match


default_config = Config()


class Node:
    _uninitialized = object()
    def __new__(cls, mv, start=0, aux_data={}, config=default_config):
        if (start >= len(mv)):
            return None
        self = super().__new__(cls)
        self.config = config
        self._next = self._uninitialized
        self.offset = None
        self.count = 1
        self.data = mv[start:]
        self.mv = mv
        self._start = start
        self._aux_data = aux_data
        self.aux_data = aux_data.get(start, None)
        return self

    @property
    def next(self):
        if self._next is self._uninitialized:
            self._next = Node(self.mv, self._start + 1,
                              aux_data=self._aux_data, config=self.config)
        return self._next

    def set_next(self, nn):
        #print(f'set_next({self},{nn})')
        self._next = nn

    def __repr__(self):
        if not self.count > 1:
            return f"[{self._start}] {self.data.tobytes().decode('ascii')[:10]}"
        return f"[{self._start}] ({self.count}, {self.offset._start}) {self.data.tobytes().decode('ascii')[:10]}"

    def dump(self):
        print(repr(self))
#        print(f"{self.data.tobytes().decode('ascii')}")
        if (self.next):
            self.next.dump()

    def match(self, other, offset):
        mrange = self.config.max_match_for_offset(offset)
        mrange = min(mrange, len(self.data), len(other.data))
        result = 0
        while(result < mrange):
            if self.data[result] != other.data[result] or self.data[result] in self.config.no_compress:
                break
            result += 1
        return result


# return node linked list and modify list of dicts in place with metainfo
def prepare_nodes(data, config=default_config):
    if isinstance(data, bytes):
        return Node(memoryview(data))
    ls = []
    offset = 0
    aux_data = {}
    for d in data:
        d['offset'] = offset
        aux_data[offset] = d
        d.setdefault('length', len(d.setdefault('data', b'')))
        ls.append(d['data'])
        ls.append(config.terminator)
        offset += len(ls[-1])
        offset += len(ls[-2])
    return Node(memoryview(b"".join(ls)), aux_data=aux_data)


def compress(s, output=sys.stdout.buffer, config=default_config):
    # we start by lazily creating a linked list of all characters in
    # the input string along with the string that comes after it.
    # we utilize a memoryview to not duplicate the bytes in memory.

    head = dptr = prepare_nodes(s, config=config)
#    head.dump()

    while(dptr):
        cl = dptr
        # we begin comparing the current node (dptr) with the next
        # nodes, following the next pointers and limiting depth
        # of search to max_offset
        for offset in range(config.max_offset):
            cl = cl.next
            if not cl:
                break
            m = dptr.match(cl, offset)
            if (m >= 2):  # if we match at least 2 bytes, try to add match
                nn = cl
                j = d = 0
                # since matches may already exist, we need to figure out how
                # many nodes we can munch into this match without
                # overshooting our character target.
                # j is the number of characters to be replaced by the match
                # d is the number of bytes (characters or existing matches)
                # that will be replaced.
                while(nn):
                    # we need to be able to directly access aux_data nodes so
                    # they should not be pulled into a match
                    if nn.aux_data:
                        break
                    c = nn.count
                    if j + c > m:
                        break
                    j += c
                    nn = nn.next
                    d += 1
                if (d >= 2):         # check if its still worth it after pruning
                    cl.set_next(nn)  # this chops out a section of the list.
                    cl.count = j     # j may be less than m if it would have broken an existing match
                    cl.offset = dptr
        dptr = dptr.next
    zero_offset = config.zero_bits
    counter = 0
    # walk the final list outputting characters or matches as we encounter
    # them.
#    head.dump()
    while(head):
        if(head.aux_data):
            head.aux_data['compressed_offset'] = counter
        if head.count > 1:
            assert head.count <= config.max_match
            the_byte = 0x80 | ((counter - head.offset.counter - 1) <<
                               config.count_bits) | ((head.count - 2) & (2**config.count_bits - 1))
            output.write(bytes([the_byte]))
        else:
            output.write(bytes([head.data[0]]))
        head.counter = counter
        counter += 1
        head = head.next


def decompress(s, start=0, howmany=(1 << 64), config=default_config, output=sys.stdout.buffer):
    needed = howmany
    slen = len(s)
    count_bits = config.count_bits
    while (needed and start < slen):
        ch = s[start]
        start += 1
        if not (ch & 0x80):
            output.write(bytes([ch]))
            needed -= 1
        else:
            offset = (ch & 0x7f) >> count_bits
            count = (ch & ((1 << count_bits) - 1)) + 2
            if (needed > count):
                needed -= decompress(s, start - offset - 2, count, output=output)
            else:
                start = start - offset - 2
    return howmany - needed

# simple utility to help output code.
class CodeWriter:
    def __init__(self, linelength=80, output=sys.stdout):
        self.indent = 0
        self.output = output
        self.linelength = linelength

    def __enter__(self):
        self.indent += 4

    def __exit__(self, typ, value, tb):
        self.indent -= 4

    def p(self, *args):
        for x in args:
            if x:
                self.output.write(" " * self.indent)
                self.output.write(str(x))
            self.output.write("\n")

    @staticmethod
    def to_dname(name):
        s = ""
        for c in str(name):
            if c not in string.ascii_letters + string.digits + "_":
                s += "_"
            else:
                s += c.upper()
        return s



    def bytes_to_string(self, name, data, prgmem = ""):
        rchar = []
        for c in data:
            z = '\a\b\f\n\r\t\v\\"'.find(chr(c))
            if z != -1:
                rchar.append("\\" + 'abfnrtv\\"'[z])
            elif chr(c) in string.printable:
                rchar.append(chr(c))
            else:
                rchar.append("\\%03o" % c)
        lines = []
        while(rchar):
            l = ""
            while(len(l) < 80 and rchar):
                l += rchar.pop(0)
            lines.append(l)
        fl = f'static const char {name}[]{prgmem} = '
        if len(lines) < 1:
            fl += '"'
            fl += "".join(lines)
            fl += '";'
            self.p(fl)
            return
        self.p(fl)
        with self:
            while lines:
                x = f'"{lines.pop(0)}"'
                if not lines:
                    x += ';'
                self.p(x)

#            self.pf('{}const char {}[]{} = "{}";',   name, prgmem, data)


def main(args):

    if args.z:
        default_config.no_compress = b"\0"
    default_config.terminator = b"\0" if args.__dict__['0'] else b""
    bs = []
    for file in args.file:
        if file == sys.stdin:
            s = file.buffer.read()
        else:
            s = file.read()
        bs.append((file.name, s))

    if args.d:
        ls = []
        for fn, s in bs:
            ls.append(s)
            if args.__dict__['0']:
                ls.append(b"\0")
        s = b"".join(ls)
        decompress(s, output=args.o)
    if args.c:
        if args.y:
            data = []
            for _, s in bs:
                data += yaml.load(s)
            for d in data:
                if 'data' in d:
                    if isinstance(d['data'],str):
                        d['data'] = d['data'].encode("ascii")
        elif args.l:
            lines = (b"".join([y for _, y in bs])).splitlines()
            data = [{'data': s, 'name': i} for i, s in enumerate(lines)]
        else:
            data = [{'name': fn, 'data': s} for fn, s in bs]

        if args.s:
            sdict = {}
            for x in data:
                sdict.setdefault(x['data'], []).append(x)
            data = []
            for k, vs in sorted(sdict.items()):
                data.append({'data': k, 'vs': vs})

        if args.f != 'raw':
            bio = io.BytesIO()
            compress(data, output=bio)
            fdata = []
            for x in data:
                del x['data']
                if 'compressed_offset' not in x and x['length'] == 0:
                    x['compressed_offset'] = 0
                if args.s:
                    for v in x['vs']:
                        v['compressed_offset'] = x['compressed_offset']
                        v['length'] = x['length']
                        del v['data']
                    fdata += x['vs']
                else:
                    fdata.append(x)
            data = fdata
            raw = bio.getvalue()
            if args.f == 'yaml':
                args.o.write(yaml.dump({ 'raw': raw , 'compressed_length': len(raw), 'parts': data}, indent=4).encode("ascii"))
            if args.f in ('c', 'c_avr') :
                tio = io.StringIO()
                c = CodeWriter(output=tio)
                c.p("#ifndef LZJWM_DATA_H")
                c.p("#define LZJWM_DATA_H")
                c.p("")
                for x in data:
                    name = x['name']
                    c.p(f'#define OFFSET_{c.to_dname(name)} {x["compressed_offset"]}')
                    c.p(f'#define LENGTH_{c.to_dname(name)} {x["length"]}')
                    c.p('')

                c.bytes_to_string('lzjwm_data', raw, " PROGMEM" if args.f == 'c_avr' else "")
                c.p("")
                c.p("#endif")

                args.o.write(tio.getvalue().encode("ascii"))

        else:
            compress(data, output=args.o)

#            print(x)
#        print(data)


if __name__ == "__main__":
    import argparse
    import sys
    parser = argparse.ArgumentParser(
        description='do the thing')
    parser = argparse.ArgumentParser(
        description='lzjwm compressor/decompressor')
    parser.add_argument('-c', action='store_true', help='compress')
    parser.add_argument('-d', action='store_true', help='decompress')
    parser.add_argument('-y', action='store_true', help='yaml input')
    parser.add_argument('-z', action='store_true',
                        help='never compress null so it appears unchanged in compressed data. useful for random access.')
    parser.add_argument('-0', action='store_true',
                        help='append a null terminator to each thing compressed.')

    parser.add_argument('--verbose', '-v', action='count', default=0)

    parser.add_argument('-l', action='store_true',
                        help='treat each line in input as its own record')
    parser.add_argument('-s', action='store_true',
                        help='attempt to rearange and unify records for better compression')
    parser.add_argument('-f', help='output format when compressing',
                        choices=('raw', 'c', 'yaml', 'c_avr'), default=['raw'])
    parser.add_argument('file', nargs='*',
                        type=argparse.FileType('rb'), default=[sys.stdin], help='input file')
    parser.add_argument('-o',
                        type=argparse.FileType('wb'), default=sys.stdout.buffer, help='output file')
    args = parser.parse_args()
    if not (args.c or args.d) or (args.c and args.d):
        print("one of -c or -d is required")
        parser.print_help(sys.stderr)
        sys.exit(1)
    main(args)


