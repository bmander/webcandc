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
from buildflags import cxxflags_for, sources  # noqa: E402

CONFIGS = {
    "debug": {"cflags": ["-O1", "-g"], "ldflags": ["-O1", "-g", "-sASSERTIONS=1"]},
    "release": {"cflags": ["-O2"], "ldflags": ["-O2"]},
    # Node test harness: no SDL window; frames dumped to $WEBCANDC_FRAMES.
    "headless": {"cflags": ["-O1", "-g", "-DWEBCANDC_HEADLESS"],
                 "ldflags": ["-O1", "-g", "-sASSERTIONS=1", "-sENVIRONMENT=node"]},
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
    "-lidbfs.js",
    "-sEXPORTED_RUNTIME_METHODS=FS,callMain,ccall",
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
    if Path(src).suffix == ".c":
        # Third-party C sources are compiled as plain C, without the 1995 environment.
        cmd = ["emcc", "-c", *cflags, "-MMD", "-MF", str(obj.with_suffix(".d")), "-o", str(obj), str(src)]
    else:
        cmd = ["em++", "-c", *cxxflags_for(src), *cflags, "-MMD", "-MF", str(obj.with_suffix(".d")), "-o", str(obj), str(src)]
    p = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, errors="replace")
    return src, p.returncode, p.stderr


WITH_MOVIES = False


def stage_data(stage):
    pkg = ROOT / "data" / "pkg"
    stage.mkdir(parents=True, exist_ok=True)
    if not pkg.exists():
        print("warning: data/pkg missing; the game will find no data files")
        return
    # stands in for the one SETUP.EXE wrote; $WEBCANDC_INI substitutes another (tests)
    ini = Path(os.environ["WEBCANDC_INI"]) if os.environ.get("WEBCANDC_INI") else ROOT / "web" / "CONQUER.INI"
    if ini.exists():
        shutil.copy2(ini, stage / "CONQUER.INI")
    # data/cd (files extracted from the freeware C&C95 disc images) wins over
    # data/pkg (the smaller DOS-edition package), file by file.
    provided = set()
    for src_dir in (ROOT / "data" / "cd", pkg):
        if not src_dir.exists():
            continue
        for f in src_dir.iterdir():
            if f.name.upper() in provided:
                continue	# an earlier (preferred) source already has this file
            provided.add(f.name.upper())
            if f.is_file() and f.suffix.upper() in (".MIX", ".INI", ".ENG", ".VQA", ".AUD"):
                dst = stage / f.name.upper()
                # Movies (MOVIES.MIX is 449 MB, plus the SIZZLE previews) are only
                # packaged on request until they are streamed on demand.
                if not WITH_MOVIES and (dst.name == "MOVIES.MIX" or dst.suffix == ".VQA"):
                    if dst.exists():
                        dst.unlink()
                    continue
                if dst.name == "CONQUER.INI":
                    continue
                if not dst.exists() or dst.stat().st_mtime < f.stat().st_mtime or dst.stat().st_size != f.stat().st_size:
                    shutil.copy2(f, dst)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--release", action="store_true")
    ap.add_argument("--headless", action="store_true", help="node test harness build")
    ap.add_argument("--undefined", action="store_true", help="report undefined symbols")
    ap.add_argument("--with-movies", action="store_true", help="package MOVIES.MIX and the VQA previews")
    ap.add_argument("--no-data", action="store_true", help="player build: no game data packaged; the page asks for it")
    ap.add_argument("-j", type=int, default=os.cpu_count())
    a = ap.parse_args()
    config = "release" if a.release else ("headless" if a.headless else "debug")
    global WITH_MOVIES
    WITH_MOVIES = a.with_movies
    cfg = CONFIGS[config]
    out = ROOT / "build" / config
    objdir = out / "obj"

    # Changed compiler flags don't show up in file timestamps: rebuild everything.
    stamp = out / "flags.txt"
    signature = repr((cxxflags_for("x.cpp"), cxxflags_for("win32.cpp"), cfg["cflags"]))
    if objdir.exists() and (not stamp.exists() or stamp.read_text() != signature):
        print("[build] compiler flags changed; rebuilding all objects")
        shutil.rmtree(objdir)
    out.mkdir(parents=True, exist_ok=True)
    stamp.write_text(signature)

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
    ldflags = [f for f in LDFLAGS if not (config == "headless" and f.startswith("-sENVIRONMENT"))]
    link = ["em++", *[str(j[1]) for j in jobs], *cfg["ldflags"], *ldflags,
            "--preload-file", f"{ROOT / 'web' / 'CONQUER.INI'}@/defaults/CONQUER.INI",
            "--js-library", str(ROOT / "src" / "platform" / "library_webcandc.js")]
    if not a.no_data:
        link += ["--preload-file", f"{stage}@/data"]
    if config != "headless":
        link.append("-sINVOKE_RUN=0")		# the page starts main once the data is in place
    if config == "headless":
        link += ["-o", str(web / "index.js")]
    else:
        link += ["--shell-file", str(ROOT / "web" / "shell.html"), "-o", str(web / "index.html")]
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
        print(f"[build] ok -> {web}")


if __name__ == "__main__":
    main()
