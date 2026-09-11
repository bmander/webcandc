#!/usr/bin/env python3
"""
Assemble the GitHub Pages site -- the player build plus the game data it
downloads on first visit -- in build/pages, and optionally publish it.

The data is the freeware disc's files minus the movies (MOVIES.MIX and the
.VQA trailers): about 65 MB with the music. It never enters the source
history: each publish force-pushes build/pages as a single commit on the
gh-pages branch, so old copies do not accumulate.

    python3 tools/build.py --release --no-data
    python3 tools/deploy_pages.py                 # assemble build/pages
    python3 tools/deploy_pages.py --push origin   # ...and publish to gh-pages
"""
import argparse
import json
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WEB = os.path.join(ROOT, "build", "release", "web")
SITE = os.path.join(ROOT, "build", "pages")
DATA_DIRS = [os.path.join(ROOT, "data", "cd"), os.path.join(ROOT, "data", "pkg")]
# Only these are copied from the build: build/*/web can hold test links to disc images.
BUILD_FILES = ["index.html", "index.js", "index.wasm", "index.data"]
LEFT_OUT = {"MOVIES.MIX"}


def git(*args, cwd=ROOT):
    return subprocess.run(["git", *args], cwd=cwd, check=True, capture_output=True, text=True).stdout.strip()


def data_files():
    """The .MIX files to host, upper-cased; data/cd wins over data/pkg."""
    chosen = {}
    for d in DATA_DIRS:
        if not os.path.isdir(d):
            continue
        for name in sorted(os.listdir(d)):
            upper = name.upper()
            if upper.endswith(".MIX") and upper not in LEFT_OUT and upper not in chosen:
                chosen[upper] = os.path.join(d, name)
    return chosen


def assemble(source_url):
    if not os.path.isfile(os.path.join(WEB, "index.wasm")):
        sys.exit("no player build: run  python3 tools/build.py --release --no-data  first")
    if os.path.exists(os.path.join(WEB, "index.data")) and os.path.getsize(os.path.join(WEB, "index.data")) > 1 << 20:
        sys.exit("build/release/web/index.data has game data packed in: rebuild with --no-data")
    shutil.rmtree(SITE, ignore_errors=True)
    os.makedirs(os.path.join(SITE, "data"))
    for name in BUILD_FILES:
        if os.path.exists(os.path.join(WEB, name)):
            shutil.copy2(os.path.join(WEB, name), SITE)
    shutil.copy2(os.path.join(ROOT, "src", "game", "LICENSE.md"), os.path.join(SITE, "LICENSE.md"))
    open(os.path.join(SITE, ".nojekyll"), "w").close()

    files = data_files()
    missing = {"CONQUER.MIX", "GENERAL.MIX", "UPDATE.MIX", "CCLOCAL.MIX", "TEMPERAT.MIX"} - set(files)
    if missing:
        sys.exit("game data incomplete, missing: " + ", ".join(sorted(missing)))
    manifest = {"source": source_url, "files": []}
    for name, path in sorted(files.items()):
        shutil.copy2(path, os.path.join(SITE, "data", name))
        manifest["files"].append({"name": name, "size": os.path.getsize(path)})
    with open(os.path.join(SITE, "data", "manifest.json"), "w") as f:
        json.dump(manifest, f, indent=1)

    total = sum(f["size"] for f in manifest["files"])
    print(f"[pages] {len(files)} data files, {total / 1e6:.1f} MB -> {SITE}")
    biggest = max(manifest["files"], key=lambda f: f["size"])
    if biggest["size"] >= 100 * 1000 * 1000:
        sys.exit(f"{biggest['name']} is over GitHub's 100 MB file limit")


def publish(remote):
    url = git("remote", "get-url", remote)
    head = git("rev-parse", "--short", "HEAD")
    git("init", "-q", "-b", "gh-pages", cwd=SITE)
    git("add", "-A", cwd=SITE)
    git("commit", "-q", "-m", f"Site build from {head}", cwd=SITE)
    print(f"[pages] pushing gh-pages to {url}")
    subprocess.run(["git", "push", "-f", url, "gh-pages"], cwd=SITE, check=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--push", metavar="REMOTE", help="publish build/pages as the gh-pages branch of REMOTE")
    ap.add_argument("--source", help="source code URL for the page footer (default: the push remote, else origin)")
    args = ap.parse_args()
    source = args.source
    if not source:
        try:
            source = git("remote", "get-url", args.push or "origin")
        except subprocess.CalledProcessError:
            source = ""
        if source.endswith(".git"):
            source = source[:-4]
    assemble(source)
    if args.push:
        publish(args.push)


if __name__ == "__main__":
    main()
