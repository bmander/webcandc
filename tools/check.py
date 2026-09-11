#!/usr/bin/env python3
"""Syntax-check every translation unit in parallel and summarize errors.

Usage: tools/check.py [--files A.CPP B.CPP] [--show N] [--by-file]
Runs em++ -fsyntax-only with the project flags; prints the most frequent
error messages and per-file error counts so header problems surface first.
"""
import argparse, collections, concurrent.futures, os, re, subprocess, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
from buildflags import CXXFLAGS, sources  # noqa: E402

ERR = re.compile(r"^(?P<file>[^:\n]+):(?P<line>\d+):\d+: (?:fatal )?error: (?P<msg>.*)$", re.M)


def run(src):
    cmd = ["em++", "-fsyntax-only", "-ferror-limit=0", *CXXFLAGS, str(src)]
    p = subprocess.run(cmd, capture_output=True, text=True, errors="replace", cwd=ROOT)
    return src, p.returncode, p.stderr


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--files", nargs="*")
    ap.add_argument("--show", type=int, default=40)
    ap.add_argument("--by-file", action="store_true")
    ap.add_argument("--raw", action="store_true", help="print full stderr for each file")
    ap.add_argument("--group", default="all", help="game|wwlib|vqa|platform|all")
    a = ap.parse_args()
    files = [ROOT / f for f in a.files] if a.files else sources(a.group)
    msgs, per_file, locs = collections.Counter(), {}, collections.defaultdict(set)
    ok = 0
    with concurrent.futures.ThreadPoolExecutor(os.cpu_count()) as ex:
        for src, rc, err in ex.map(run, files):
            if a.raw and err.strip():
                print(f"===== {src.relative_to(ROOT)}\n{err}")
            found = list(ERR.finditer(err))
            if rc == 0:
                ok += 1
            per_file[src.name] = len(found) if found else (0 if rc == 0 else -1)
            for m in found:
                msg = re.sub(r"'[^']*'", "'…'", m["msg"]) if not a.raw else m["msg"]
                msgs[m["msg"]] += 1
                locs[m["msg"]].add(f"{Path(m['file']).name}:{m['line']}")
    print(f"{ok}/{len(files)} files compile cleanly")
    for msg, n in msgs.most_common(a.show):
        where = sorted(locs[msg])
        print(f"{n:5d}  {msg}   [{', '.join(where[:3])}{' …' if len(where) > 3 else ''}]")
    if a.by_file:
        for f, n in sorted(per_file.items(), key=lambda kv: -kv[1]):
            if n:
                print(f"  {n:5d} {f}")


if __name__ == "__main__":
    main()
