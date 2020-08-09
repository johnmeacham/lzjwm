#!/usr/bin/python3

from tabulate import tabulate
import os
import sys
import glob
import subprocess
from pathlib import PurePath

log = open("regress.log", "a")
log_command = False


def call(args, result, status, stdin=sys.stdin, stdout=log):
    if isinstance(stdin, str) and isinstance(stdout, str):
        with open(stdin) as fh:
            with open(stdout, 'w') as fh2:
                return _call(args, result, status, stdin=fh, stdout=fh2)
    return _call(args, result, status, stdin=stdin, stdout=stdout)


def _call(args, result, old_status, stdin=sys.stdin, stdout=log):
    if old_status == 'SKIP':
        result.append('-')
        return old_status
    if old_status:
        result.append('X')
        return 'X'
    if log_command:
        log.write("; " + " ".join(args) + "\n")
    sys.stdout.write("; " + " ".join(args) + "\n")
    log.flush()
    status = subprocess.call(args, stderr=log, stdin=stdin, stdout=stdout)
    result.append(status)
    # log.write("status: " + str(status) + "\n")
    # log.flush()
    return status


results = []

log.write("\n-----------------\n")
log.flush()
subprocess.call(['./lzjwm', '-v'], stdout=log)
log.flush()


base = 'regress'
for fn in sorted(glob.glob(base + '/*')):
    if not os.path.isfile(fn):
        continue
    log.write("test: " + fn + "\n")
    log.flush()
    pp = PurePath(fn)
    baseout = base + '/out/' + pp.name
    result = [pp.name]
    results.append(result)
    status = call(['./lzjwm', '-c'], result, None,
                  stdin=str(pp), stdout=baseout + '.lzjwm')
    status = call(['./lzjwm', '-x'], result, status,
                  stdin=baseout + '.lzjwm', stdout=baseout + '.dump')
    status = call(['./lzjwm', '-d'], result, status,
                  stdin=baseout + '.lzjwm', stdout=baseout + '.decompressed')
    status = call(['./lzjwm', '-S'], result, status, stdin=baseout +
                  '.lzjwm', stdout=baseout + '.decompressed_stream')
    status = call(['diff', baseout + '.decompressed', fn], result, status)
    status = call(
        ['diff', baseout + '.decompressed_stream', fn], result, status)
    status = call(['./lzjwm.py', '-d', baseout + '.lzjwm', '-o', baseout + '.decompressed_python'], result, status)
    status = call(
        ['diff', baseout + '.decompressed_python', fn], result, status)
    status = call(['./lzjwm.py', '-c', fn, '-o', baseout + '.lzjwm_python'], result, status)
    status = call(['./lzjwm', '-d'], result, status,
                  stdin=baseout + '.lzjwm_python', stdout=baseout + '.decompressed_c')
    status = call(
        ['diff', baseout + '.decompressed_c', fn], result, status)


tab = tabulate(results, ['name', 'compress', 'decompress',
                         'decompress_stream', 'diff', 'diff_stream', 'decom_python', 'diff_python','comp_python','decom_c','diff_p2c'])
log.write(tab)
log.flush()
print(tab)
