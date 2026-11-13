#!/usr/bin/env python3
"""Syntax-check the inline scripts of the web flasher (flasher/index.html) and the
miner's web UI (main/web/index.html) with node --check.

One stray quote in a script stops all of it: the flasher then shows no boards, the
web UI no data. This catches that before a build or a release ships it.
"""
import os
import re
import subprocess
import sys
import tempfile

here = os.path.dirname(os.path.abspath(__file__))
pages = ['flasher/index.html', 'main/web/index.html']

bad = 0
for rel in pages:
    html = open(os.path.join(here, '..', rel), encoding='utf-8').read()
    for i, (attrs, body) in enumerate(re.findall(r'<script(?![^>]*\bsrc=)([^>]*)>(.*?)</script>', html, re.S)):
        ext = '.mjs' if 'module' in attrs else '.js'
        with tempfile.NamedTemporaryFile('w', suffix=ext, delete=False, encoding='utf-8') as f:
            f.write(body)
        r = subprocess.run(['node', '--check', f.name], capture_output=True, text=True)
        os.unlink(f.name)
        if r.returncode:
            bad += 1
            print(f'{rel} script {i}:\n{r.stderr}', file=sys.stderr)

print('page scripts:', 'broken' if bad else 'ok')
sys.exit(1 if bad else 0)
