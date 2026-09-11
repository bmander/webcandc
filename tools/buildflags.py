"""Shared compiler flags and source lists for tools/check.py (mirrors CMakeLists.txt)."""
import os
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# Engine headers come before game headers: wwlib32.h pulls in <mouse.h>,
# <audio.h>, ... by angle brackets and must get the engine's copies, while
# game code includes its own same-named headers with quotes (found first in
# the including file's directory).
INCLUDES = [
    "src/compat",
    "src/wwlib",
    "src/vqa/INCLUDE",
    "src/game",
    "src/platform",
]

CXXFLAGS = [
    "-std=gnu++14",
    "-fno-strict-aliasing",
    "-fwrapv",
    "-fsigned-char",
    "-fms-extensions",
    "-sUSE_SDL=2",
    "-Wno-everything",
    "-include", "src/compat/prelude.h",
    "-DTRUE_FALSE_DEFINED",
    "-DWIN32",
    "-DWEBCANDC",
    "-DWEBCANDC_NO_NET",
    *[f"-I{i}" for i in INCLUDES],
]

# Watcom sized every enum to the smallest integer type that holds its values
# (no -ei), and the game reads and writes structures containing enums straight
# from disk (e.g. MapClass::Read_Binary's {TemplateType, TIcon} pairs), so the
# 1995 code is built with -fshort-enums. Files that include SDL headers keep
# natural enums: SDL's own structs contain enums and its library is prebuilt.
# The Win32/DirectX structures shared between the two sides contain no enums.
NATURAL_ENUM_FILES = {"win32.cpp", "ddraw.cpp", "dsound.cpp"}


def cxxflags_for(src):
    """Compiler flags for one translation unit."""
    name = Path(src).name
    return CXXFLAGS + ([] if name in NATURAL_ENUM_FILES else ["-fshort-enums"])

# Files from the original MAKEFILE that we compile out entirely (DOS/IPX/modem/DDE).
# Only the Greenleaf serial/modem code: IPX, Winsock and DDE compile against the
# platform layer, which reports "no network" so the game disables those modes.
GAME_EXCLUDE = {"WINASM", "NULLDLG", "NULLCONN", "NULLMGR"}

# Object list from src/game/MAKEFILE (OBJECTS), minus assembly objects.
GAME_OBJECTS = """SUPER AADATA WINSTUB WINASM ABSTRACT ADATA AIRCRAFT ANIM AUDIO BASE BBDATA BDATA
BUILDING BULLET CARGO CCFILE CDATA CDFILE CELL CHECKBOX CHEKLIST COLRLIST COMBAT COMBUF CONFDLG
CONNECT CONQUER CONST CONTROL COORD CREDITS CREW DEBUG DIAL8 DIALOG DISPLAY DOOR DRIVE EDIT EVENT
ENDING EXPAND FACING FACTORY FINDPATH FLASHER FLY FOOT FUSE GADGET GAMEDLG GAUGE GLOBALS GOPTIONS
GSCREEN HDATA HEAP HELP HOUSE IDATA INFANTRY INI INIT INTERNET INTERPAL INTRO IOMAP IOOBJ IPX
IPXADDR IPXCONN IPXGCONN IPXMGR IPX95 JSHELL KEYFRAME LAYER LINK LIST LOADDLG LOGIC MAP
MAPEDDLG MAPEDIT MAPEDPLC MAPEDTM MAPSEL MENUS MISSION MIXFILE MOUSE MPLAYER MSGBOX MSGLIST NETDLG
NOSEQCON NULLCONN NULLDLG NULLMGR OBJECT ODATA OPTIONS OVERLAY POWER PROFILE QUEUE RADAR RADIO RAND
REINF SAVELOAD SCENARIO SCORE SCROLL SDATA SHAPEBTN SIDEBAR SLIDER SMUDGE SOUNDDLG SPECIAL STARTUP
TAB TARCOM TARGET TCPIP TDATA TEAM TEAMTYPE TECHNO TEMPLATE TERRAIN TEXTBTN THEME TOGGLE TRIGGER
TURRET TXTLABEL UDATA UNIT VECTOR VISUDLG UTRACKER PACKET FIELD STATS CCDDE DDE""".split()

# Game files the MAKEFILE took from the Westwood library, plus C++ replacements
# for the game's .ASM files.
GAME_ADDED = ["RAWFILE", "MONOC", "MISCASM", "KEYFBUFF", "TXTPRNT", "WINASM"]

# The movie player: the parts of Red Alert's WINVQ library (src/vqa) that
# C&C's configuration uses, and C++ replacements for the VQA32/VQM32
# assembly they call. Captions, the DOS stream handler, mono display and the
# VQM32 video/file code are not needed.
VQA_SOURCES = [
    "src/vqa/VQA32/AUDIO.CPP",
    "src/vqa/VQA32/CONFIG.CPP",
    "src/vqa/VQA32/DRAWER.CPP",
    "src/vqa/VQA32/LOADER.CPP",
    "src/vqa/VQA32/TASK.CPP",
    "src/vqa/VQA32/UNVQBUFF.CPP",	# UNVQBUFF.ASM
    "src/vqa/VQM32/VQMASM.CPP",	# VB.ASM, TESTVB.CPP, MCGABUF.ASM, SOSCODEC.ASM, AUDUNZAP.ASM
]

# Every engine source is compiled (the Remastered project's list, plus the
# C++ ports of its assembly that live alongside).


def _find(d, stem):
    for ext in (".CPP", ".cpp"):
        p = ROOT / d / (stem + ext)
        if p.exists():
            return p
    raise FileNotFoundError(f"{d}/{stem}")


def sources(group="all"):
    """Every translation unit in the build. $WEBCANDC_EXCLUDE (comma-separated
    stems) leaves files out, e.g. while a port of them is in progress."""
    skip = {x.strip().upper() for x in os.environ.get("WEBCANDC_EXCLUDE", "").split(",") if x.strip()}
    out = _sources(group)
    out = [p for p in out if p.stem.upper() not in skip]
    # $WEBCANDC_EXTRA (comma-separated paths) adds files, e.g. a stand-in.
    out += [ROOT / x.strip() for x in os.environ.get("WEBCANDC_EXTRA", "").split(",") if x.strip()]
    return out


def _sources(group="all"):
    out = []
    if group in ("game", "all"):
        out += [_find("src/game", s) for s in GAME_OBJECTS if s not in GAME_EXCLUDE]
        out += [p for p in (ROOT / "src/game" / (s + ".CPP") for s in GAME_ADDED) if p.exists()]
    if group in ("wwlib", "all"):
        out += sorted(p for p in (ROOT / "src/wwlib").iterdir() if p.suffix in (".CPP", ".cpp"))
    if group in ("vqa", "all"):
        out += [ROOT / p for p in VQA_SOURCES]
    if group in ("platform", "all"):
        out += sorted((ROOT / "src/platform").glob("*.cpp"))
        out += sorted((ROOT / "src/platform").glob("*.c"))		# third-party C (blast.c)
        out += sorted((ROOT / "src/compat").glob("*.cpp"))
    return out
