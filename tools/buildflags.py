"""Shared compiler flags and source lists for tools/check.py (mirrors CMakeLists.txt)."""
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
    "-Wno-everything",
    "-include", "src/compat/prelude.h",
    "-DTRUE_FALSE_DEFINED",
    "-DWIN32",
    "-DWEBCANDC",
    "-DWEBCANDC_NO_NET",
    *[f"-I{i}" for i in INCLUDES],
]

# Files from the original MAKEFILE that we compile out entirely (DOS/IPX/modem/DDE).
GAME_EXCLUDE = {
    "WINASM", "IPX", "IPX95", "IPXADDR", "IPXCONN", "IPXGCONN", "IPXMGR", "NULLDLG",
    "NULLCONN", "NULLMGR", "NOSEQCON", "TCPIP", "CCDDE", "DDE", "INTERNET",
}

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

# Engine files compiled by the Remastered TIBERIANDAWN.VCXPROJ, minus platform files we replace.
WWLIB_SOURCES = """ALLOC BUFFER BUFFGLBL DIPTHONG DrawMisc DRAWRECT FONT GBUFFER GETSHAPE ICONSET
IFF IRANDOM KEYBOARD LOAD LOADFONT LOADPAL MEM MORPHPAL MOUSEWW NEWDEL PALETTE REGIONSZ SET_FONT
TIMER TIMERDWN TIMERINI WINHIDE WRITEPCX WSA _DIPTABL""".split()


def _find(d, stem):
    for ext in (".CPP", ".cpp"):
        p = ROOT / d / (stem + ext)
        if p.exists():
            return p
    raise FileNotFoundError(f"{d}/{stem}")


def sources(group="all"):
    out = []
    if group in ("game", "all"):
        out += [_find("src/game", s) for s in GAME_OBJECTS if s not in GAME_EXCLUDE]
    if group in ("wwlib", "all"):
        out += [_find("src/wwlib", s) for s in WWLIB_SOURCES]
    if group in ("platform", "all"):
        out += sorted((ROOT / "src/platform").glob("*.cpp"))
    return out
