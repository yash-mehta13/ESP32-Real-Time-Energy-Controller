#!/usr/bin/env python3
"""Check relative links/images in tracked Markdown files and that every
asset referenced exists. Offline; external URLs are not fetched."""
import pathlib
import re
import subprocess
import sys

root = pathlib.Path(subprocess.check_output(
    ["git", "rev-parse", "--show-toplevel"], text=True).strip())
files = subprocess.check_output(
    ["git", "ls-files", "--cached", "--others", "--exclude-standard", "*.md"],
    cwd=root, text=True).split()

link_re = re.compile(r"!?\[[^\]]*\]\(([^)\s]+)(?:\s+\"[^\"]*\")?\)")
errors = 0
for name in files:
    path = root / name
    if not path.exists():
        continue
    in_code = False
    for lineno, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if line.strip().startswith("```"):
            in_code = not in_code
            continue
        if in_code:
            continue
        for target in link_re.findall(line):
            if re.match(r"^(https?:|mailto:|#)", target):
                continue
            rel = target.split("#")[0]
            if not rel:
                continue
            if not (path.parent / rel).resolve().exists():
                print(f"{name}:{lineno}: broken link -> {target}")
                errors += 1

print(f"checked {len(files)} markdown files, {errors} broken link(s)")
sys.exit(1 if errors else 0)
