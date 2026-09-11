#!/usr/bin/env python3
"""Incremental Emscripten build of webcandc.

    tools/build.py            compile changed sources, link build/web/webcandc.html
    tools/build.py --release  optimized build (separate object directory)
    tools/build.py --undefined  link and list undefined symbols instead of failing

Objects go to build/<config>/obj with -MMD dependency files, so only sources
whose inputs changed are recompiled. Game data from data/pkg is staged into
build/<config>/stage with upper-case names (the game asks for CONQUER.MIX;
the Emscripten file system is case-sensitive) and preloaded at /data.
"""
import argparse, concurrent.futures, os, re, shutil, subprocess, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
from buildflags import CXXFLAGS, sources  # noqa: E402

CONFIGS = {
    "debug": {"cflags": ["-O1", "-g"], "ldflags": ["-O1", "-g", "-sASSERTIONS=1"]},
    "release": {"cflags": ["-O2"], "ldflags": ["-O2"]},
}

LDFLAGS = [
    "-sUSE_SDL=2",
    "-sASYNCIFY",
    "-sASYNCIFY_STACK_SIZE=131072",
    "-sALLOW_MEMORY_GROWTH=1",
    "-sINITIAL_MEMORY=67108864",
    "-sSTACK_SIZE=1048576",
    "-sFORCE_FILESYSTEM=1",
    "-sEXIT_RUNTIME=0",
    "-sENVIRONMENT=web",
]


def obj_path(objdir, src):
    return objdir / (src.relative_to(ROOT).as_posix().replace("/", "_") + ".o")


def stale(src, obj):
    if not obj.exists():
        return True
    dep = obj.with_suffix(".d")
    if not dep.exists():
        return True
    mtime = obj.stat().st_mtime
    text = dep.read_text(errors="replace").replace("\\\n", " ")
    deps = re.split(r"(?<!\\)\s+", text.split(":", 1)[1]) if ":" in text else []
    for d in deps:
        d = d.replace("\\ ", " ").strip()
        if not d:
            continue
        p = Path(d) if os.path.isabs(d) else ROOT / d
        try:
            if p.stat().st_mtime > mtime:
                return True
        except FileNotFoundError:
            return True
    return False


def compile_one(args):
    src, obj, cflags = args
    obj.parent.mkdir(parents=True, exist_ok=True)
    cmd = ["em++", "-c", *CXXFLAGS, *cflags, "-MMD", "-MF", str(obj.with_suffix(".d")), "-o", str(obj), str(src)]
    p = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, errors="replace")
    return src, p.returncode, p.stderr


def stage_data(stage):
    pkg = ROOT / "data" / "pkg"
    stage.mkdir(parents=True, exist_ok=True)
    if not pkg.exists():
        print("warning: data/pkg missing; the game will find no data files")
        return
    for f in pkg.iterdir():
        if f.is_file():
            dst = stage / f.name.upper()
            if not dst.exists() or dst.stat().st_mtime < f.stat().st_mtime:
                shutil.copy2(f, dst)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--release", action="store_true")
    ap.add_argument("--undefined", action="store_true", help="report undefined symbols")
    ap.add_argument("-j", type=int, default=os.cpu_count())
    a = ap.parse_args()
    config = "release" if a.release else "debug"
    cfg = CONFIGS[config]
    out = ROOT / "build" / config
    objdir = out / "obj"

    srcs = sources("all")
    jobs = [(s, obj_path(objdir, s), cfg["cflags"]) for s in srcs]
    todo = [j for j in jobs if stale(j[0], j[1])]
    print(f"[build] {config}: {len(todo)}/{len(jobs)} sources to compile")
    failed = []
    with concurrent.futures.ThreadPoolExecutor(a.j) as ex:
        for src, rc, err in ex.map(compile_one, todo):
            if rc != 0:
                failed.append(src)
                sys.stderr.write(f"===== {src.relative_to(ROOT)}\n{err}\n")
    if failed:
        print(f"[build] {len(failed)} files failed to compile: " + " ".join(s.name for s in failed))
        sys.exit(1)

    stage = out / "stage"
    stage_data(stage)
    web = out / "web"
    web.mkdir(parents=True, exist_ok=True)
    link = ["em++", *[str(j[1]) for j in jobs], *cfg["ldflags"], *LDFLAGS,
            "--preload-file", f"{stage}@/data",
            "--shell-file", str(ROOT / "web" / "shell.html"),
            "-o", str(web / "index.html")]
    if a.undefined:
        link.insert(1, "-Wl,--warn-unresolved-symbols")
        link.append("-sERROR_ON_UNDEFINED_SYMBOLS=0")
    print("[build] linking")
    p = subprocess.run(link, cwd=ROOT, capture_output=True, text=True, errors="replace")
    if a.undefined:
        syms = sorted(set(re.findall(r"undefined symbol: (.+?)(?: \(referenced by|$)", p.stderr, re.M)))
        print(f"[build] {len(syms)} undefined symbols")
        for s in syms:
            print("  " + s)
    elif p.returncode != 0:
        sys.stderr.write(p.stderr)
        sys.exit(1)
    else:
        if p.stderr.strip():
            sys.stderr.write(p.stderr)
        print(f"[build] ok -> {web / 'index.html'}")


if __name__ == "__main__":
    main()
