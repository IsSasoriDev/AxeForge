#!/usr/bin/env python3
"""Syntax-check the inline scripts of flasher/index.html with node --check.

One stray quote in the board list stops the whole script, and the flasher then
shows no boards at all. This catches that before a release ships it.
"""
import os
import re
import subprocess
import sys
import tempfile

here = os.path.dirname(os.path.abspath(__file__))
page = os.path.join(here, '..', 'flasher', 'index.html')
html = open(page, encoding='utf-8').read()

bad = 0
for i, (attrs, body) in enumerate(re.findall(r'<script(?![^>]*\bsrc=)([^>]*)>(.*?)</script>', html, re.S)):
    ext = '.mjs' if 'module' in attrs else '.js'
    with tempfile.NamedTemporaryFile('w', suffix=ext, delete=False, encoding='utf-8') as f:
        f.write(body)
    r = subprocess.run(['node', '--check', f.name], capture_output=True, text=True)
    os.unlink(f.name)
    if r.returncode:
        bad += 1
        print(f'flasher/index.html script {i}:\n{r.stderr}', file=sys.stderr)

print('flasher scripts:', 'broken' if bad else 'ok')
sys.exit(1 if bad else 0)
