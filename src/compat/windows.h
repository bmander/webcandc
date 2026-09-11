/*
** webcandc: the slice of the Win32 API that Tiberian Dawn (Win95) and its
** Westwood library use, declared for wasm32.
**
** Types and constants match the real headers where layout matters. Functions
** are implemented in src/platform on top of SDL2 and Emscripten; anything the
** browser has no equivalent for (registry, DDE, window management) is inert.
*/
#ifndef WEBCANDC_WINDOWS_H
#define WEBCANDC_WINDOWS_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define WINAPI
#define APIENTRY
#define CALLBACK
#define PASCAL
#define FAR
#define NEAR
#define CONST const
#define VOID void
#define IN
#define OUT
#define OPTIONAL
#define WINUSERAPI
/* Non-STRICT Win32: every handle type is just HANDLE, as the 1995 code assumes. */
#define DECLARE_HANDLE(name) typedef HANDLE name

#ifndef NULL
#define NULL 0
#endif
#ifndef FALSE
#define FALSE 0
#endif
#ifndef TRUE
#define TRUE 1
#endif

typedef int BOOL;
typedef unsigned char BYTE;
typedef unsigned char UCHAR;
typedef unsigned short WORD;
typedef unsigned short USHORT;
typedef unsigned long DWORD;		/* 32 bits on wasm32, as on Win32. */
typedef unsigned long ULONG;
typedef long LONG;
typedef int INT;
typedef unsigned int UINT;
typedef short SHORT;
typedef char CHAR;
typedef char TCHAR;
typedef float FLOAT;
typedef long long LONGLONG;
typedef unsigned long long ULONGLONG;
typedef unsigned long long DWORDLONG;
typedef BYTE BOOLEAN;
typedef void *PVOID;
typedef void *LPVOID;
typedef const void *LPCVOID;
typedef char *LPSTR;
typedef char *PSTR;
typedef const char *LPCSTR;
typedef char *LPTSTR;
typedef const char *LPCTSTR;
typedef BYTE *LPBYTE;
typedef BYTE *PBYTE;
typedef WORD *LPWORD;
typedef DWORD *LPDWORD;
typedef DWORD *PDWORD;
typedef LONG *LPLONG;
typedef BOOL *LPBOOL;
typedef int *LPINT;
typedef UINT *PUINT;
typedef long LPARAM;
typedef unsigned int WPARAM;
typedef long LRESULT;
typedef long HRESULT;
typedef unsigned short ATOM;
typedef DWORD COLORREF;
typedef void *HANDLE;
typedef int (WINAPI *FARPROC)(void);
typedef HANDLE *LPHANDLE;
typedef HANDLE HGLOBAL;
typedef HANDLE HLOCAL;
typedef int HFILE;

DECLARE_HANDLE(HWND);
DECLARE_HANDLE(HINSTANCE);
DECLARE_HANDLE(HDC);
DECLARE_HANDLE(HPALETTE);
DECLARE_HANDLE(HBITMAP);
DECLARE_HANDLE(HMENU);
DECLARE_HANDLE(HICON);
DECLARE_HANDLE(HBRUSH);
DECLARE_HANDLE(HFONT);
DECLARE_HANDLE(HKEY);
DECLARE_HANDLE(HGDIOBJ);
DECLARE_HANDLE(HRGN);
DECLARE_HANDLE(HPEN);
DECLARE_HANDLE(HACCEL);
DECLARE_HANDLE(HRSRC);
DECLARE_HANDLE(HCONV);
DECLARE_HANDLE(HSZ);
DECLARE_HANDLE(HDDEDATA);
DECLARE_HANDLE(HMMIO);
typedef HICON HCURSOR;
typedef HINSTANCE HMODULE;
typedef HKEY *PHKEY;

#define HFILE_ERROR ((HFILE)-1)
#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)
#define MAX_PATH 260
#define INFINITE 0xFFFFFFFF

#define MAKEWORD(a, b) ((WORD)(((BYTE)(a)) | ((WORD)((BYTE)(b))) << 8))
#define MAKELONG(a, b) ((LONG)(((WORD)(a)) | ((DWORD)((WORD)(b))) << 16))
#define LOWORD(l) ((WORD)((DWORD)(l) & 0xFFFF))
#define HIWORD(l) ((WORD)((DWORD)(l) >> 16))
#define LOBYTE(w) ((BYTE)((w) & 0xFF))
#define HIBYTE(w) ((BYTE)(((WORD)(w) >> 8) & 0xFF))
#define RGB(r, g, b) ((COLORREF)(((BYTE)(r) | ((WORD)((BYTE)(g)) << 8)) | (((DWORD)(BYTE)(b)) << 16)))
#define MAKEINTRESOURCE(i) ((LPSTR)((DWORD)((WORD)(i))))
#define SUCCEEDED(hr) ((HRESULT)(hr) >= 0)
#define FAILED(hr) ((HRESULT)(hr) < 0)
#define S_OK ((HRESULT)0)
#define S_FALSE ((HRESULT)1)
#define E_FAIL ((HRESULT)0x80004005L)
#define E_OUTOFMEMORY ((HRESULT)0x8007000EL)
#define E_INVALIDARG ((HRESULT)0x80070057L)
#define E_NOINTERFACE ((HRESULT)0x80004002L)
#define E_NOTIMPL ((HRESULT)0x80004001L)

typedef struct tagRECT { LONG left, top, right, bottom; } RECT, *LPRECT, *PRECT;
typedef const RECT *LPCRECT;
typedef struct tagPOINT { LONG x, y; } POINT, *LPPOINT, *PPOINT;
typedef struct tagSIZE { LONG cx, cy; } SIZE, *LPSIZE;
typedef struct tagMSG { HWND hwnd; UINT message; WPARAM wParam; LPARAM lParam; DWORD time; POINT pt; } MSG, *LPMSG, *PMSG;
typedef struct tagPALETTEENTRY { BYTE peRed, peGreen, peBlue, peFlags; } PALETTEENTRY, *LPPALETTEENTRY, *PPALETTEENTRY;
typedef struct tagLOGPALETTE { WORD palVersion; WORD palNumEntries; PALETTEENTRY palPalEntry[1]; } LOGPALETTE;
typedef struct tagRGBQUAD { BYTE rgbBlue, rgbGreen, rgbRed, rgbReserved; } RGBQUAD;
typedef struct _RGNDATAHEADER { DWORD dwSize, iType, nCount, nRgnSize; RECT rcBound; } RGNDATAHEADER;
typedef struct _RGNDATA { RGNDATAHEADER rdh; char Buffer[1]; } RGNDATA, *LPRGNDATA;
typedef struct tagPAINTSTRUCT { HDC hdc; BOOL fErase; RECT rcPaint; BOOL fRestore; BOOL fIncUpdate; BYTE rgbReserved[32]; } PAINTSTRUCT, *LPPAINTSTRUCT;
typedef struct _SYSTEMTIME { WORD wYear, wMonth, wDayOfWeek, wDay, wHour, wMinute, wSecond, wMilliseconds; } SYSTEMTIME, *LPSYSTEMTIME;
typedef struct _FILETIME { DWORD dwLowDateTime, dwHighDateTime; } FILETIME, *LPFILETIME;
typedef struct _MEMORYSTATUS { DWORD dwLength, dwMemoryLoad, dwTotalPhys, dwAvailPhys, dwTotalPageFile, dwAvailPageFile, dwTotalVirtual, dwAvailVirtual; } MEMORYSTATUS, *LPMEMORYSTATUS;
typedef struct _OSVERSIONINFO { DWORD dwOSVersionInfoSize, dwMajorVersion, dwMinorVersion, dwBuildNumber, dwPlatformId; CHAR szCSDVersion[128]; } OSVERSIONINFO, *LPOSVERSIONINFO;
typedef struct _CRITICAL_SECTION { int unused; } CRITICAL_SECTION, *LPCRITICAL_SECTION;
typedef struct _SECURITY_ATTRIBUTES { DWORD nLength; LPVOID lpSecurityDescriptor; BOOL bInheritHandle; } SECURITY_ATTRIBUTES, *LPSECURITY_ATTRIBUTES;
typedef struct _OVERLAPPED { DWORD Internal, InternalHigh, Offset, OffsetHigh; HANDLE hEvent; } OVERLAPPED, *LPOVERLAPPED;
typedef struct _WIN32_FIND_DATA {
	DWORD dwFileAttributes; FILETIME ftCreationTime, ftLastAccessTime, ftLastWriteTime;
	DWORD nFileSizeHigh, nFileSizeLow, dwReserved0, dwReserved1;
	CHAR cFileName[MAX_PATH]; CHAR cAlternateFileName[14];
} WIN32_FIND_DATA, *LPWIN32_FIND_DATA;
typedef union _LARGE_INTEGER { struct { DWORD LowPart; LONG HighPart; } u; struct { DWORD LowPart; LONG HighPart; }; LONGLONG QuadPart; } LARGE_INTEGER;
typedef struct _GUID { DWORD Data1; WORD Data2, Data3; BYTE Data4[8]; } GUID, IID, CLSID;
typedef GUID *LPGUID;
typedef const GUID *LPCGUID;
typedef const GUID &REFGUID;
typedef const IID &REFIID;
typedef const CLSID &REFCLSID;
#define DEFINE_GUID(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8) static const GUID name = { l, w1, w2, { b1, b2, b3, b4, b5, b6, b7, b8 } }

typedef LRESULT (CALLBACK *WNDPROC)(HWND, UINT, WPARAM, LPARAM);
typedef BOOL (CALLBACK *DLGPROC)(HWND, UINT, WPARAM, LPARAM);
typedef void (CALLBACK *TIMERPROC)(HWND, UINT, UINT, DWORD);
typedef DWORD (WINAPI *LPTHREAD_START_ROUTINE)(LPVOID);

typedef struct tagWNDCLASS {
	UINT style; WNDPROC lpfnWndProc; int cbClsExtra; int cbWndExtra; HINSTANCE hInstance;
	HICON hIcon; HCURSOR hCursor; HBRUSH hbrBackground; LPCSTR lpszMenuName; LPCSTR lpszClassName;
} WNDCLASS, *LPWNDCLASS;

/* Window messages. */
#define WM_NULL             0x0000
#define WM_CREATE           0x0001
#define WM_DESTROY          0x0002
#define WM_MOVE             0x0003
#define WM_SIZE             0x0005
#define WM_ACTIVATE         0x0006
#define WM_SETFOCUS         0x0007
#define WM_KILLFOCUS        0x0008
#define WM_PAINT            0x000F
#define WM_CLOSE            0x0010
#define WM_QUIT             0x0012
#define WM_ERASEBKGND       0x0014
#define WM_SHOWWINDOW       0x0018
#define WM_ACTIVATEAPP      0x001C
#define WM_SETCURSOR        0x0020
#define WM_NCACTIVATE       0x0086
#define WM_KEYDOWN          0x0100
#define WM_KEYUP            0x0101
#define WM_CHAR             0x0102
#define WM_SYSKEYDOWN       0x0104
#define WM_SYSKEYUP         0x0105
#define WM_SYSCHAR          0x0106
#define WM_INITDIALOG       0x0110
#define WM_COMMAND          0x0111
#define WM_SYSCOMMAND       0x0112
#define WM_TIMER            0x0113
#define WM_MOUSEMOVE        0x0200
#define WM_LBUTTONDOWN      0x0201
#define WM_LBUTTONUP        0x0202
#define WM_LBUTTONDBLCLK    0x0203
#define WM_RBUTTONDOWN      0x0204
#define WM_RBUTTONUP        0x0205
#define WM_RBUTTONDBLCLK    0x0206
#define WM_MBUTTONDOWN      0x0207
#define WM_MBUTTONUP        0x0208
#define WM_MBUTTONDBLCLK    0x0209
#define WM_MOUSEWHEEL       0x020A
#define WM_PALETTECHANGED   0x0311
#define WM_QUERYNEWPALETTE  0x030F
#define WM_USER             0x0400

#define PM_NOREMOVE 0x0000
#define PM_REMOVE   0x0001
#define PM_NOYIELD  0x0002

/* Virtual key codes. */
#define VK_LBUTTON  0x01
#define VK_RBUTTON  0x02
#define VK_CANCEL   0x03
#define VK_MBUTTON  0x04
#define VK_BACK     0x08
#define VK_TAB      0x09
#define VK_CLEAR    0x0C
#define VK_RETURN   0x0D
#define VK_SHIFT    0x10
#define VK_CONTROL  0x11
#define VK_MENU     0x12
#define VK_PAUSE    0x13
#define VK_CAPITAL  0x14
#define VK_ESCAPE   0x1B
#define VK_SPACE    0x20
#define VK_PRIOR    0x21
#define VK_NEXT     0x22
#define VK_END      0x23
#define VK_HOME     0x24
#define VK_LEFT     0x25
#define VK_UP       0x26
#define VK_RIGHT    0x27
#define VK_DOWN     0x28
#define VK_SELECT   0x29
#define VK_PRINT    0x2A
#define VK_EXECUTE  0x2B
#define VK_SNAPSHOT 0x2C
#define VK_INSERT   0x2D
#define VK_DELETE   0x2E
#define VK_HELP     0x2F
#define VK_LWIN     0x5B
#define VK_RWIN     0x5C
#define VK_APPS     0x5D
#define VK_NUMPAD0  0x60
#define VK_NUMPAD1  0x61
#define VK_NUMPAD2  0x62
#define VK_NUMPAD3  0x63
#define VK_NUMPAD4  0x64
#define VK_NUMPAD5  0x65
#define VK_NUMPAD6  0x66
#define VK_NUMPAD7  0x67
#define VK_NUMPAD8  0x68
#define VK_NUMPAD9  0x69
#define VK_MULTIPLY 0x6A
#define VK_ADD      0x6B
#define VK_SEPARATOR 0x6C
#define VK_SUBTRACT 0x6D
#define VK_DECIMAL  0x6E
#define VK_DIVIDE   0x6F
#define VK_F1       0x70
#define VK_F2       0x71
#define VK_F3       0x72
#define VK_F4       0x73
#define VK_F5       0x74
#define VK_F6       0x75
#define VK_F7       0x76
#define VK_F8       0x77
#define VK_F9       0x78
#define VK_F10      0x79
#define VK_F11      0x7A
#define VK_F12      0x7B
#define VK_NUMLOCK  0x90
#define VK_SCROLL   0x91
#define VK_LSHIFT   0xA0
#define VK_RSHIFT   0xA1
#define VK_LCONTROL 0xA2
#define VK_RCONTROL 0xA3
#define VK_LMENU    0xA4
#define VK_RMENU    0xA5

/* Message boxes. */
#define MB_OK               0x0000
#define MB_OKCANCEL         0x0001
#define MB_ABORTRETRYIGNORE 0x0002
#define MB_YESNOCANCEL      0x0003
#define MB_YESNO            0x0004
#define MB_RETRYCANCEL      0x0005
#define MB_ICONHAND         0x0010
#define MB_ICONQUESTION     0x0020
#define MB_ICONEXCLAMATION  0x0030
#define MB_ICONASTERISK     0x0040
#define MB_ICONERROR        MB_ICONHAND
#define MB_ICONSTOP         MB_ICONHAND
#define MB_ICONWARNING      MB_ICONEXCLAMATION
#define MB_ICONINFORMATION  MB_ICONASTERISK
#define MB_SYSTEMMODAL      0x1000
#define MB_TASKMODAL        0x2000
#define MB_SETFOREGROUND    0x10000
#define MB_TOPMOST          0x40000
#define IDOK     1
#define IDCANCEL 2
#define IDABORT  3
#define IDRETRY  4
#define IDIGNORE 5
#define IDYES    6
#define IDNO     7

/* ShowWindow / window styles / class styles. */
#define SW_HIDE 0
#define SW_SHOWNORMAL 1
#define SW_NORMAL 1
#define SW_SHOWMINIMIZED 2
#define SW_SHOWMAXIMIZED 3
#define SW_MAXIMIZE 3
#define SW_SHOW 5
#define SW_MINIMIZE 6
#define SW_RESTORE 9
#define WS_POPUP 0x80000000L
#define WS_VISIBLE 0x10000000L
#define WS_MAXIMIZE 0x01000000L
#define WS_OVERLAPPEDWINDOW 0x00CF0000L
#define WS_EX_TOPMOST 0x00000008L
#define CS_HREDRAW 0x0002
#define CS_VREDRAW 0x0001
#define CS_DBLCLKS 0x0008
#define IDC_ARROW MAKEINTRESOURCE(32512)
#define IDC_WAIT MAKEINTRESOURCE(32514)
#define IDI_APPLICATION MAKEINTRESOURCE(32512)
#define BLACK_BRUSH 4
#define SC_SCREENSAVE 0xF140
#define SC_CLOSE 0xF060
#define SC_KEYMENU 0xF100
#define WA_INACTIVE 0
#define WA_ACTIVE 1
#define WA_CLICKACTIVE 2
#define SM_CXSCREEN 0
#define SM_CYSCREEN 1

/* Files. */
#define GENERIC_READ 0x80000000L
#define GENERIC_WRITE 0x40000000L
#define FILE_SHARE_READ 0x00000001
#define FILE_SHARE_WRITE 0x00000002
#define CREATE_NEW 1
#define CREATE_ALWAYS 2
#define OPEN_EXISTING 3
#define OPEN_ALWAYS 4
#define TRUNCATE_EXISTING 5
#define FILE_ATTRIBUTE_READONLY 0x00000001
#define FILE_ATTRIBUTE_HIDDEN 0x00000002
#define FILE_ATTRIBUTE_SYSTEM 0x00000004
#define FILE_ATTRIBUTE_DIRECTORY 0x00000010
#define FILE_ATTRIBUTE_ARCHIVE 0x00000020
#define FILE_ATTRIBUTE_NORMAL 0x00000080
#define FILE_FLAG_SEQUENTIAL_SCAN 0x08000000
#define FILE_FLAG_RANDOM_ACCESS 0x10000000
#define FILE_BEGIN 0
#define FILE_CURRENT 1
#define FILE_END 2
#define DRIVE_UNKNOWN 0
#define DRIVE_NO_ROOT_DIR 1
#define DRIVE_REMOVABLE 2
#define DRIVE_FIXED 3
#define DRIVE_REMOTE 4
#define DRIVE_CDROM 5
#define DRIVE_RAMDISK 6
#define ERROR_SUCCESS 0L
#define ERROR_FILE_NOT_FOUND 2L
#define ERROR_ACCESS_DENIED 5L
#define NO_ERROR 0L

/* Memory. */
#define GMEM_FIXED 0x0000
#define GMEM_MOVEABLE 0x0002
#define GMEM_ZEROINIT 0x0040
#define GPTR (GMEM_FIXED | GMEM_ZEROINIT)
#define GHND (GMEM_MOVEABLE | GMEM_ZEROINIT)
#define LMEM_FIXED 0x0000
#define LMEM_ZEROINIT 0x0040
#define LPTR (LMEM_FIXED | LMEM_ZEROINIT)
#define MEM_COMMIT 0x1000
#define MEM_RESERVE 0x2000
#define MEM_RELEASE 0x8000
#define PAGE_READWRITE 0x04

/* Threads / sync / priority. */
#define THREAD_PRIORITY_NORMAL 0
#define THREAD_PRIORITY_ABOVE_NORMAL 1
#define THREAD_PRIORITY_HIGHEST 2
#define THREAD_PRIORITY_TIME_CRITICAL 15
#define NORMAL_PRIORITY_CLASS 0x20
#define HIGH_PRIORITY_CLASS 0x80
#define REALTIME_PRIORITY_CLASS 0x100
#define WAIT_OBJECT_0 0
#define WAIT_TIMEOUT 258L

/* Registry. */
#define HKEY_CLASSES_ROOT ((HKEY)(uintptr_t)0x80000000)
#define HKEY_CURRENT_USER ((HKEY)(uintptr_t)0x80000001)
#define HKEY_LOCAL_MACHINE ((HKEY)(uintptr_t)0x80000002)
#define KEY_READ 0x20019
#define KEY_WRITE 0x20006
#define KEY_ALL_ACCESS 0xF003F
#define REG_SZ 1
#define REG_BINARY 3
#define REG_DWORD 4
#define REG_OPTION_NON_VOLATILE 0

/* Multimedia timers (mmsystem.h). */
typedef UINT MMRESULT;
typedef void (CALLBACK *LPTIMECALLBACK)(UINT uTimerID, UINT uMsg, DWORD dwUser, DWORD dw1, DWORD dw2);
typedef struct timecaps_tag { UINT wPeriodMin, wPeriodMax; } TIMECAPS, *LPTIMECAPS;
#define TIMERR_NOERROR 0
#define TIME_ONESHOT 0x0000
#define TIME_PERIODIC 0x0001
#define TIME_CALLBACK_FUNCTION 0x0000
#define TIME_KILL_SYNCHRONOUS 0x0100
#define MMSYSERR_NOERROR 0

/* Wave formats (mmsystem.h), needed by DSOUND.H. */
typedef struct tWAVEFORMATEX {
	WORD wFormatTag; WORD nChannels; DWORD nSamplesPerSec; DWORD nAvgBytesPerSec;
	WORD nBlockAlign; WORD wBitsPerSample; WORD cbSize;
} WAVEFORMATEX, *LPWAVEFORMATEX, *PWAVEFORMATEX;
typedef const WAVEFORMATEX *LPCWAVEFORMATEX;
typedef struct pcmwaveformat_tag { WORD wFormatTag; WORD nChannels; DWORD nSamplesPerSec; DWORD nAvgBytesPerSec; WORD nBlockAlign; WORD wBitsPerSample; } PCMWAVEFORMAT;
#define WAVE_FORMAT_PCM 1

#ifdef __cplusplus
extern "C" {
#endif

/* Time. */
DWORD WINAPI GetTickCount(void);
DWORD WINAPI timeGetTime(void);
MMRESULT WINAPI timeBeginPeriod(UINT period);
MMRESULT WINAPI timeEndPeriod(UINT period);
MMRESULT WINAPI timeGetDevCaps(LPTIMECAPS caps, UINT size);
MMRESULT WINAPI timeSetEvent(UINT delay, UINT resolution, LPTIMECALLBACK proc, DWORD user, UINT flags);
MMRESULT WINAPI timeKillEvent(UINT id);
void WINAPI Sleep(DWORD ms);
void WINAPI GetLocalTime(LPSYSTEMTIME st);
void WINAPI GetSystemTime(LPSYSTEMTIME st);
BOOL WINAPI QueryPerformanceCounter(LARGE_INTEGER *count);
BOOL WINAPI QueryPerformanceFrequency(LARGE_INTEGER *freq);

/* Messages and windows. */
BOOL WINAPI PeekMessage(LPMSG msg, HWND wnd, UINT first, UINT last, UINT remove);
BOOL WINAPI GetMessage(LPMSG msg, HWND wnd, UINT first, UINT last);
BOOL WINAPI TranslateMessage(const MSG *msg);
LRESULT WINAPI DispatchMessage(const MSG *msg);
BOOL WINAPI PostMessage(HWND wnd, UINT msg, WPARAM w, LPARAM l);
LRESULT WINAPI SendMessage(HWND wnd, UINT msg, WPARAM w, LPARAM l);
void WINAPI PostQuitMessage(int code);
LRESULT WINAPI DefWindowProc(HWND wnd, UINT msg, WPARAM w, LPARAM l);
ATOM WINAPI RegisterClass(const WNDCLASS *wc);
HWND WINAPI CreateWindow(LPCSTR cls, LPCSTR name, DWORD style, int x, int y, int w, int h, HWND parent, HMENU menu, HINSTANCE inst, LPVOID param);
HWND WINAPI CreateWindowEx(DWORD exstyle, LPCSTR cls, LPCSTR name, DWORD style, int x, int y, int w, int h, HWND parent, HMENU menu, HINSTANCE inst, LPVOID param);
BOOL WINAPI DestroyWindow(HWND wnd);
BOOL WINAPI ShowWindow(HWND wnd, int cmd);
BOOL WINAPI UpdateWindow(HWND wnd);
HWND WINAPI SetFocus(HWND wnd);
HWND WINAPI GetFocus(void);
HWND WINAPI GetActiveWindow(void);
HWND WINAPI SetActiveWindow(HWND wnd);
BOOL WINAPI SetForegroundWindow(HWND wnd);
HWND WINAPI GetForegroundWindow(void);
HWND WINAPI FindWindow(LPCSTR cls, LPCSTR name);
BOOL WINAPI IsWindow(HWND wnd);
BOOL WINAPI IsIconic(HWND wnd);
BOOL WINAPI InvalidateRect(HWND wnd, const RECT *rect, BOOL erase);
BOOL WINAPI GetClientRect(HWND wnd, LPRECT rect);
BOOL WINAPI GetWindowRect(HWND wnd, LPRECT rect);
BOOL WINAPI MoveWindow(HWND wnd, int x, int y, int w, int h, BOOL repaint);
HDC WINAPI BeginPaint(HWND wnd, LPPAINTSTRUCT ps);
BOOL WINAPI EndPaint(HWND wnd, const PAINTSTRUCT *ps);
HDC WINAPI GetDC(HWND wnd);
int WINAPI ReleaseDC(HWND wnd, HDC dc);
UINT WINAPI SetTimer(HWND wnd, UINT id, UINT elapse, TIMERPROC proc);
BOOL WINAPI KillTimer(HWND wnd, UINT id);
int WINAPI MessageBox(HWND wnd, LPCSTR text, LPCSTR caption, UINT type);
int WINAPI GetSystemMetrics(int index);
HCURSOR WINAPI LoadCursor(HINSTANCE inst, LPCSTR name);
HCURSOR WINAPI SetCursor(HCURSOR cursor);
int WINAPI ShowCursor(BOOL show);
BOOL WINAPI GetCursorPos(LPPOINT pt);
BOOL WINAPI SetCursorPos(int x, int y);
BOOL WINAPI ClipCursor(const RECT *rect);
HWND WINAPI SetCapture(HWND wnd);
BOOL WINAPI ReleaseCapture(void);
BOOL WINAPI ScreenToClient(HWND wnd, LPPOINT pt);
BOOL WINAPI ClientToScreen(HWND wnd, LPPOINT pt);
HICON WINAPI LoadIcon(HINSTANCE inst, LPCSTR name);
HGDIOBJ WINAPI GetStockObject(int obj);
short WINAPI GetAsyncKeyState(int vkey);
short WINAPI GetKeyState(int vkey);
UINT WINAPI MapVirtualKey(UINT code, UINT type);
int WINAPI ToAscii(UINT vkey, UINT scan, const BYTE *state, LPWORD out, UINT flags);
BOOL WINAPI GetKeyboardState(PBYTE state);
SHORT WINAPI VkKeyScan(CHAR ch);
int WINAPI DialogBox(HINSTANCE inst, LPCSTR tmpl, HWND parent, DLGPROC proc);
BOOL WINAPI EndDialog(HWND dlg, int result);
HWND WINAPI GetDlgItem(HWND dlg, int id);
BOOL WINAPI SetDlgItemText(HWND dlg, int id, LPCSTR text);
LRESULT WINAPI SendDlgItemMessage(HWND dlg, int id, UINT msg, WPARAM w, LPARAM l);
BOOL WINAPI SetWindowText(HWND wnd, LPCSTR text);

/* Process, modules, errors. */
HMODULE WINAPI GetModuleHandle(LPCSTR name);
DWORD WINAPI GetModuleFileName(HMODULE mod, LPSTR buf, DWORD size);
DWORD WINAPI GetLastError(void);
void WINAPI SetLastError(DWORD err);
void WINAPI OutputDebugString(LPCSTR text);
void WINAPI ExitProcess(UINT code);
HANDLE WINAPI GetCurrentProcess(void);
HANDLE WINAPI GetCurrentThread(void);
DWORD WINAPI GetCurrentThreadId(void);
BOOL WINAPI SetPriorityClass(HANDLE process, DWORD cls);
DWORD WINAPI GetPriorityClass(HANDLE process);
BOOL WINAPI SetThreadPriority(HANDLE thread, int priority);
HANDLE WINAPI CreateThread(LPSECURITY_ATTRIBUTES sa, DWORD stack, LPTHREAD_START_ROUTINE start, LPVOID param, DWORD flags, LPDWORD id);
HANDLE WINAPI CreateEvent(LPSECURITY_ATTRIBUTES sa, BOOL manual, BOOL initial, LPCSTR name);
BOOL WINAPI SetEvent(HANDLE ev);
BOOL WINAPI ResetEvent(HANDLE ev);
HANDLE WINAPI CreateMutex(LPSECURITY_ATTRIBUTES sa, BOOL owner, LPCSTR name);
BOOL WINAPI ReleaseMutex(HANDLE mutex);
DWORD WINAPI WaitForSingleObject(HANDLE h, DWORD ms);
BOOL WINAPI CloseHandle(HANDLE h);
void WINAPI InitializeCriticalSection(LPCRITICAL_SECTION cs);
void WINAPI DeleteCriticalSection(LPCRITICAL_SECTION cs);
void WINAPI EnterCriticalSection(LPCRITICAL_SECTION cs);
void WINAPI LeaveCriticalSection(LPCRITICAL_SECTION cs);
BOOL WINAPI GetVersionEx(LPOSVERSIONINFO info);
void WINAPI GlobalMemoryStatus(LPMEMORYSTATUS status);
UINT WINAPI WinExec(LPCSTR cmd, UINT show);
UINT WINAPI RegisterWindowMessage(LPCSTR name);
HMODULE WINAPI LoadLibrary(LPCSTR name);
BOOL WINAPI FreeLibrary(HMODULE mod);
FARPROC WINAPI GetProcAddress(HMODULE mod, LPCSTR name);
BOOL WINAPI IsBadReadPtr(const void *ptr, UINT size);
BOOL WINAPI IsBadWritePtr(void *ptr, UINT size);
HINSTANCE WINAPI ShellExecute(HWND wnd, LPCSTR op, LPCSTR file, LPCSTR params, LPCSTR dir, int show);

/* Memory. */
HGLOBAL WINAPI GlobalAlloc(UINT flags, DWORD bytes);
HGLOBAL WINAPI GlobalFree(HGLOBAL mem);
LPVOID WINAPI GlobalLock(HGLOBAL mem);
BOOL WINAPI GlobalUnlock(HGLOBAL mem);
DWORD WINAPI GlobalSize(HGLOBAL mem);
HGLOBAL WINAPI GlobalReAlloc(HGLOBAL mem, DWORD bytes, UINT flags);
HLOCAL WINAPI LocalAlloc(UINT flags, UINT bytes);
HLOCAL WINAPI LocalFree(HLOCAL mem);
LPVOID WINAPI VirtualAlloc(LPVOID addr, DWORD size, DWORD type, DWORD protect);
BOOL WINAPI VirtualFree(LPVOID addr, DWORD size, DWORD type);
BOOL WINAPI VirtualLock(LPVOID addr, DWORD size);
BOOL WINAPI VirtualUnlock(LPVOID addr, DWORD size);

/* Files and directories. */
HANDLE WINAPI CreateFile(LPCSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD disposition, DWORD flags, HANDLE tmpl);
BOOL WINAPI ReadFile(HANDLE f, LPVOID buf, DWORD n, LPDWORD read, LPOVERLAPPED ov);
BOOL WINAPI WriteFile(HANDLE f, LPCVOID buf, DWORD n, LPDWORD written, LPOVERLAPPED ov);
DWORD WINAPI SetFilePointer(HANDLE f, LONG dist, LPLONG high, DWORD method);
DWORD WINAPI GetFileSize(HANDLE f, LPDWORD high);
BOOL WINAPI GetFileTime(HANDLE f, LPFILETIME created, LPFILETIME accessed, LPFILETIME written);
BOOL WINAPI SetFileTime(HANDLE f, const FILETIME *created, const FILETIME *accessed, const FILETIME *written);
BOOL WINAPI DeleteFile(LPCSTR name);
BOOL WINAPI CopyFile(LPCSTR from, LPCSTR to, BOOL fail_if_exists);
BOOL WINAPI MoveFile(LPCSTR from, LPCSTR to);
DWORD WINAPI GetFileAttributes(LPCSTR name);
BOOL WINAPI SetFileAttributes(LPCSTR name, DWORD attr);
HANDLE WINAPI FindFirstFile(LPCSTR pattern, LPWIN32_FIND_DATA data);
BOOL WINAPI FindNextFile(HANDLE find, LPWIN32_FIND_DATA data);
BOOL WINAPI FindClose(HANDLE find);
DWORD WINAPI GetCurrentDirectory(DWORD size, LPSTR buf);
BOOL WINAPI SetCurrentDirectory(LPCSTR dir);
BOOL WINAPI CreateDirectory(LPCSTR dir, LPSECURITY_ATTRIBUTES sa);
UINT WINAPI GetDriveType(LPCSTR root);
DWORD WINAPI GetLogicalDrives(void);
BOOL WINAPI GetVolumeInformation(LPCSTR root, LPSTR vol, DWORD volsize, LPDWORD serial, LPDWORD maxlen, LPDWORD flags, LPSTR fs, DWORD fssize);
BOOL WINAPI GetDiskFreeSpace(LPCSTR root, LPDWORD spc, LPDWORD bps, LPDWORD free, LPDWORD total);
UINT WINAPI GetWindowsDirectory(LPSTR buf, UINT size);
UINT WINAPI GetSystemDirectory(LPSTR buf, UINT size);
DWORD WINAPI GetPrivateProfileString(LPCSTR sec, LPCSTR key, LPCSTR def, LPSTR buf, DWORD size, LPCSTR file);
UINT WINAPI GetPrivateProfileInt(LPCSTR sec, LPCSTR key, int def, LPCSTR file);
BOOL WINAPI WritePrivateProfileString(LPCSTR sec, LPCSTR key, LPCSTR val, LPCSTR file);
HFILE WINAPI _lopen(LPCSTR name, int mode);
HFILE WINAPI _lclose(HFILE f);
UINT WINAPI _lread(HFILE f, LPVOID buf, UINT n);
LONG WINAPI _llseek(HFILE f, LONG ofs, int origin);

/* Registry. */
LONG WINAPI RegOpenKeyEx(HKEY key, LPCSTR sub, DWORD opts, DWORD sam, PHKEY out);
LONG WINAPI RegOpenKey(HKEY key, LPCSTR sub, PHKEY out);
LONG WINAPI RegCreateKeyEx(HKEY key, LPCSTR sub, DWORD res, LPSTR cls, DWORD opts, DWORD sam, LPSECURITY_ATTRIBUTES sa, PHKEY out, LPDWORD disp);
LONG WINAPI RegCreateKey(HKEY key, LPCSTR sub, PHKEY out);
LONG WINAPI RegQueryValueEx(HKEY key, LPCSTR name, LPDWORD res, LPDWORD type, LPBYTE data, LPDWORD size);
LONG WINAPI RegSetValueEx(HKEY key, LPCSTR name, DWORD res, DWORD type, const BYTE *data, DWORD size);
LONG WINAPI RegCloseKey(HKEY key);

/* GDI (palette and blits are routed through DirectDraw instead). */
UINT WINAPI GetSystemPaletteEntries(HDC dc, UINT start, UINT count, LPPALETTEENTRY entries);
HPALETTE WINAPI CreatePalette(const LOGPALETTE *pal);
BOOL WINAPI DeleteObject(HGDIOBJ obj);
HGDIOBJ WINAPI SelectObject(HDC dc, HGDIOBJ obj);
int WINAPI GetDeviceCaps(HDC dc, int index);

#ifdef __cplusplus
}
#endif

/*
** Winsock 1.1 (TCPIP.CPP). The BSD socket calls come from the C library
** (headers included by the prelude); WSAStartup reports that no network
** stack is available, so the game never gets as far as opening a socket.
*/
typedef unsigned int SOCKET;
typedef struct in_addr IN_ADDR;
typedef struct sockaddr_in SOCKADDR_IN;
typedef struct sockaddr SOCKADDR, *LPSOCKADDR;
typedef struct hostent HOSTENT, *LPHOSTENT;
typedef struct linger LINGER;
typedef struct WSAData { WORD wVersion, wHighVersion; char szDescription[257]; char szSystemStatus[129]; unsigned short iMaxSockets, iMaxUdpDg; char *lpVendorInfo; } WSADATA, *LPWSADATA;
#define MAXGETHOSTSTRUCT 1024
#define INVALID_SOCKET ((SOCKET)(~0))
#define SOCKET_ERROR (-1)
#define FD_READ    0x01
#define FD_WRITE   0x02
#define FD_OOB     0x04
#define FD_ACCEPT  0x08
#define FD_CONNECT 0x10
#define FD_CLOSE   0x20
#define WSAGETSELECTEVENT(l) LOWORD(l)
#define WSAGETSELECTERROR(l) HIWORD(l)
#define WSAGETASYNCERROR(l)  HIWORD(l)
#define WSAGETASYNCBUFLEN(l) LOWORD(l)
#define WSAEWOULDBLOCK  10035
#define WSAECONNRESET   10054
#define WSASYSNOTREADY  10091

/* DDEML (DDE.CPP / CCDDE.CPP, the WChat link). DdeInitialize fails. */
typedef HDDEDATA (CALLBACK *PFNCALLBACK)(UINT, UINT, HCONV, HSZ, HSZ, HDDEDATA, DWORD, DWORD);
typedef struct tagCONVCONTEXT { UINT cb; UINT wFlags; UINT wCountryID; int iCodePage; DWORD dwLangID; DWORD dwSecurity; SECURITY_ATTRIBUTES qos; } CONVCONTEXT, *PCONVCONTEXT;
#define CP_WINANSI 1004
#define APPCLASS_STANDARD 0x00000000L
#define CBF_FAIL_SELFCONNECTIONS 0x00001000
#define DMLERR_NO_ERROR 0
#define DMLERR_DLL_NOT_INITIALIZED 0x4003
#define DNS_REGISTER 0x0001
#define DNS_UNREGISTER 0x0002
#define CF_TEXT 1
#define DDE_FACK 0x8000
#define DDE_FBUSY 0x4000
#define DDE_FNOTPROCESSED 0x0000
#define SZDDESYS_TOPIC "System"
#define XTYP_ADVDATA 0x4010
#define XTYP_CONNECT 0x1062
#define XTYP_DISCONNECT 0x00C2
#define XTYP_EXECUTE 0x4050
#define XTYP_POKE 0x4090
#define XTYP_REGISTER 0x80A2
#define XTYP_UNREGISTER 0x80D2
#define XTYP_XACT_COMPLETE 0x8080
#define TIMEOUT_ASYNC 0xFFFFFFFF

/* Processes (INTERNET.CPP launching WChat). CreateProcess fails. */
typedef struct _STARTUPINFO {
	DWORD cb; LPSTR lpReserved, lpDesktop, lpTitle; DWORD dwX, dwY, dwXSize, dwYSize, dwXCountChars, dwYCountChars, dwFillAttribute, dwFlags;
	WORD wShowWindow, cbReserved2; LPBYTE lpReserved2; HANDLE hStdInput, hStdOutput, hStdError;
} STARTUPINFO, *LPSTARTUPINFO;
typedef struct _PROCESS_INFORMATION { HANDLE hProcess, hThread; DWORD dwProcessId, dwThreadId; } PROCESS_INFORMATION, *LPPROCESS_INFORMATION;
#define STARTF_USESHOWWINDOW 0x00000001

#ifdef __cplusplus
extern "C" {
#endif
int WINAPI WSAStartup(WORD version, LPWSADATA data);
int WINAPI WSACleanup(void);
int WINAPI WSAGetLastError(void);
int WINAPI WSAAsyncSelect(SOCKET s, HWND wnd, UINT msg, long events);
HANDLE WINAPI WSAAsyncGetHostByName(HWND wnd, UINT msg, const char *name, char *buf, int buflen);
HANDLE WINAPI WSAAsyncGetHostByAddr(HWND wnd, UINT msg, const char *addr, int len, int type, char *buf, int buflen);
int WINAPI WSACancelAsyncRequest(HANDLE request);
int WINAPI closesocket(SOCKET s);
UINT WINAPI DdeInitialize(LPDWORD inst, PFNCALLBACK callback, DWORD cmd, DWORD res);
BOOL WINAPI DdeUninitialize(DWORD inst);
HSZ WINAPI DdeCreateStringHandle(DWORD inst, LPCSTR str, int codepage);
BOOL WINAPI DdeFreeStringHandle(DWORD inst, HSZ hsz);
DWORD WINAPI DdeQueryString(DWORD inst, HSZ hsz, LPSTR buf, DWORD max, int codepage);
HDDEDATA WINAPI DdeNameService(DWORD inst, HSZ s1, HSZ s2, UINT cmd);
HCONV WINAPI DdeConnect(DWORD inst, HSZ service, HSZ topic, void *context);
BOOL WINAPI DdeDisconnect(HCONV conv);
HDDEDATA WINAPI DdeClientTransaction(LPBYTE data, DWORD len, HCONV conv, HSZ item, UINT fmt, UINT type, DWORD timeout, LPDWORD result);
LPBYTE WINAPI DdeAccessData(HDDEDATA data, LPDWORD size);
BOOL WINAPI DdeUnaccessData(HDDEDATA data);
BOOL WINAPI DdeFreeDataHandle(HDDEDATA data);
BOOL WINAPI CreateProcess(LPCSTR app, LPSTR cmd, LPSECURITY_ATTRIBUTES pa, LPSECURITY_ATTRIBUTES ta, BOOL inherit, DWORD flags, LPVOID env, LPCSTR dir, LPSTARTUPINFO si, LPPROCESS_INFORMATION pi);
LONG WINAPI RegQueryValue(HKEY key, LPCSTR sub, LPSTR value, LONG *size);
#ifdef __cplusplus
}

/* Winsock took int lengths where BSD sockets take socklen_t. */
inline int getsockopt(SOCKET s, int level, int name, char *val, int *len)
{ socklen_t l = (socklen_t)*len; int r = ::getsockopt((int)s, level, name, val, &l); *len = (int)l; return r; }
inline int recvfrom(SOCKET s, char *buf, int len, int flags, struct sockaddr *from, int *fromlen)
{ socklen_t l = (socklen_t)*fromlen; int r = (int)::recvfrom((int)s, buf, (size_t)len, flags, from, &l); *fromlen = (int)l; return r; }
inline SOCKET accept(SOCKET s, struct sockaddr *addr, int *addrlen)
{ socklen_t l = addrlen ? (socklen_t)*addrlen : 0; int r = ::accept((int)s, addr, addrlen ? &l : NULL); if (addrlen) *addrlen = (int)l; return (SOCKET)r; }
#endif

/*
** webcandc platform hooks (src/platform). The "operating system" runs when
** the game looks at the clock or pumps messages: WebCandC_Service() polls
** input, fires due timeSetEvent callbacks, and every frame's worth of time
** presents the primary surface and yields to the browser.
*/
#ifdef __cplusplus
extern "C" {
#endif
void WebCandC_Init(void);
void WebCandC_Service(void);
void WebCandC_Yield(void);
void WebCandC_Present(void);
void WebCandC_Mark_Screen_Dirty(void);
#ifdef __cplusplus
}
#endif

#define CreateWindowA CreateWindow
#define MessageBoxA MessageBox
#define wsprintf sprintf
#define lstrcpy strcpy
#define lstrcat strcat
#define lstrlen strlen
#define lstrcmpi stricmp
#define lstrcmp strcmp
#define ZeroMemory(p, n) memset((p), 0, (n))
#define FillMemory(p, n, v) memset((p), (v), (n))
#define CopyMemory(d, s, n) memcpy((d), (s), (n))
#define MoveMemory(d, s, n) memmove((d), (s), (n))

#endif
