#!/usr/bin/env python3
"""Heuristic scan of tracked/unignored files for committed credentials.
Flags assignments of non-placeholder values to credential-like names and a
few well-known token shapes. It prints file:line and the rule, never the
matched value."""
import pathlib
import re
import subprocess
import sys

root = pathlib.Path(subprocess.check_output(
    ["git", "rev-parse", "--show-toplevel"], text=True).strip())
files = subprocess.check_output(
    ["git", "ls-files", "--cached", "--others", "--exclude-standard"],
    cwd=root, text=True).split()

PLACEHOLDER = re.compile(r"YOUR_|CHANGE_?ME|<[^>]*>|example|placeholder|xxx", re.I)
assign = re.compile(
    r"(WIFI_PASSWORD|MQTT_PASSWORD|MQTT_USERNAME|BLYNK_AUTH_TOKEN|WIFI_SSID|password|secret|token)"
    r"\s*(\[\s*\])?\s*[:=]?\s*\"([^\"]+)\"", re.I)
shapes = {
    "private key block": re.compile(r"-----BEGIN (RSA |EC |OPENSSH )?PRIVATE KEY-----"),
    "AWS access key id": re.compile(r"AKIA[0-9A-Z]{16}"),
    "GitHub token": re.compile(r"gh[pousr]_[A-Za-z0-9]{30,}"),
}

problems = 0
for name in files:
    p = root / name
    if not p.is_file() or p.suffix in {".png", ".jpg", ".svg"} or name == "scripts/scan_secrets.py":
        continue
    try:
        text = p.read_text(encoding="utf-8")
    except (UnicodeDecodeError, OSError):
        continue
    for lineno, line in enumerate(text.splitlines(), 1):
        for label, rx in shapes.items():
            if rx.search(line):
                print(f"{name}:{lineno}: {label}")
                problems += 1
        m = assign.search(line)
        if m and not PLACEHOLDER.search(m.group(3)):
            print(f"{name}:{lineno}: non-placeholder value assigned to {m.group(1)}")
            problems += 1

print(f"scanned {len(files)} files, {problems} finding(s)")
sys.exit(1 if problems else 0)
