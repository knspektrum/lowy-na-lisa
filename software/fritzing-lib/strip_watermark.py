"""Removes the "fritzing" watermark group from a Fritzing SVG export, in place."""
import re
import sys

path = sys.argv[1]
t = open(path, encoding="utf-8").read()
start = t.index('<g id="watermark"')
depth, pos = 0, start
for m in re.compile(r"<g\b[^>]*?(/?)>|</g>").finditer(t, start):
    if m.group(0).startswith("</g"):
        depth -= 1
    elif not m.group(1):
        depth += 1
    if depth == 0:
        pos = m.end()
        break
open(path, "w", encoding="utf-8").write(t[:start] + t[pos:])
