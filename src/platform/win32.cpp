/*
** webcandc: the Win32 API slice used by Tiberian Dawn, on SDL2 + Emscripten.
**
** Model: one window, one message queue. SDL events become WM_* messages that
** the game's own Windows_Procedure (WINSTUB.CPP) receives through
** DispatchMessage, exactly as on Windows 95. Multimedia timers (timeSetEvent)
** are fired from WebCandC_Service(), which runs whenever the game pumps
** messages or reads the clock, so the 1995 busy-wait loops keep working.
*/
#include <windows.h>
#include <objbase.h>
WEBCANDC_SYSTEM_HEADERS_BEGIN
#include <SDL2/SDL.h>
#include <emscripten.h>
WEBCANDC_SYSTEM_HEADERS_END
#include <deque>
#include <vector>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/*
** ---- Time -----------------------------------------------------------------
*/
static double Start_Ms = -1;

static double Now_Ms(void)
{
	double now = emscripten_get_now();
	if (Start_Ms < 0) Start_Ms = now;
	return now - Start_Ms;
}

extern "C" DWORD WINAPI GetTickCount(void) { WebCandC_Service(); return (DWORD)Now_Ms(); }
extern "C" DWORD WINAPI timeGetTime(void) { WebCandC_Service(); return (DWORD)Now_Ms(); }
extern "C" MMRESULT WINAPI timeBeginPeriod(UINT) { return TIMERR_NOERROR; }
extern "C" MMRESULT WINAPI timeEndPeriod(UINT) { return TIMERR_NOERROR; }

extern "C" MMRESULT WINAPI timeGetDevCaps(LPTIMECAPS caps, UINT)
{
	caps->wPeriodMin = 1;
	caps->wPeriodMax = 1000000;
	return TIMERR_NOERROR;
}

struct MMTimer {
	UINT Id;
	UINT Period;
	LPTIMECALLBACK Proc;
	DWORD User;
	bool Periodic;
	double Due;
};
static std::vector<MMTimer> Timers;
static UINT NextTimerId = 1;

extern "C" MMRESULT WINAPI timeSetEvent(UINT delay, UINT, LPTIMECALLBACK proc, DWORD user, UINT flags)
{
	MMTimer t;
	t.Id = NextTimerId++;
	t.Period = delay ? delay : 1;
	t.Proc = proc;
	t.User = user;
	t.Periodic = (flags & TIME_PERIODIC) != 0;
	t.Due = Now_Ms() + t.Period;
	Timers.push_back(t);
	return t.Id;
}

extern "C" MMRESULT WINAPI timeKillEvent(UINT id)
{
	for (size_t i = 0; i < Timers.size(); i++) {
		if (Timers[i].Id == id) {
			Timers.erase(Timers.begin() + i);
			return TIMERR_NOERROR;
		}
	}
	return 1;
}

/*
** Fire every timer that has come due. A periodic timer that fell behind
** (the tab was in the background) catches up by at most one second so the
** game clock doesn't lurch.
*/
static void Fire_Timers(void)
{
	double now = Now_Ms();
	for (size_t i = 0; i < Timers.size(); i++) {
		if (Timers[i].Due > now) continue;
		UINT id = Timers[i].Id;
		if (!Timers[i].Periodic) {
			MMTimer t = Timers[i];
			Timers.erase(Timers.begin() + i);
			i--;
			t.Proc(t.Id, 0, t.User, 0, 0);
			continue;
		}
		if (now - Timers[i].Due > 1000.0) Timers[i].Due = now - 1000.0;
		while (i < Timers.size() && Timers[i].Id == id && Timers[i].Due <= now) {
			Timers[i].Due += Timers[i].Period;
			Timers[i].Proc(id, 0, Timers[i].User, 0, 0);
		}
	}
}

extern "C" void WINAPI Sleep(DWORD ms)
{
	double until = Now_Ms() + ms;
	do {
		WebCandC_Yield();
	} while (Now_Ms() < until);
}

extern "C" void WINAPI GetLocalTime(LPSYSTEMTIME st)
{
	time_t t = time(NULL);
	struct tm *tm = localtime(&t);
	st->wYear = (WORD)(tm->tm_year + 1900);
	st->wMonth = (WORD)(tm->tm_mon + 1);
	st->wDayOfWeek = (WORD)tm->tm_wday;
	st->wDay = (WORD)tm->tm_mday;
	st->wHour = (WORD)tm->tm_hour;
	st->wMinute = (WORD)tm->tm_min;
	st->wSecond = (WORD)tm->tm_sec;
	st->wMilliseconds = 0;
}

extern "C" void WINAPI GetSystemTime(LPSYSTEMTIME st) { GetLocalTime(st); }

extern "C" BOOL WINAPI QueryPerformanceCounter(LARGE_INTEGER *count)
{
	count->QuadPart = (LONGLONG)(Now_Ms() * 1000.0);
	return TRUE;
}

extern "C" BOOL WINAPI QueryPerformanceFrequency(LARGE_INTEGER *freq)
{
	freq->QuadPart = 1000000;
	return TRUE;
}

/*
** ---- Keyboard -------------------------------------------------------------
*/
static unsigned char KeyDown[256];

static int SDL_To_VK(SDL_Keycode k)
{
	if (k >= SDLK_a && k <= SDLK_z) return 'A' + (k - SDLK_a);
	if (k >= SDLK_0 && k <= SDLK_9) return '0' + (k - SDLK_0);
	if (k >= SDLK_F1 && k <= SDLK_F12) return VK_F1 + (k - SDLK_F1);
	if (k >= SDLK_KP_1 && k <= SDLK_KP_9) return VK_NUMPAD1 + (k - SDLK_KP_1);
	switch (k) {
		case SDLK_RETURN: case SDLK_KP_ENTER: return VK_RETURN;
		case SDLK_ESCAPE: return VK_ESCAPE;
		case SDLK_BACKSPACE: return VK_BACK;
		case SDLK_TAB: return VK_TAB;
		case SDLK_SPACE: return VK_SPACE;
		case SDLK_LEFT: return VK_LEFT;
		case SDLK_RIGHT: return VK_RIGHT;
		case SDLK_UP: return VK_UP;
		case SDLK_DOWN: return VK_DOWN;
		case SDLK_INSERT: return VK_INSERT;
		case SDLK_DELETE: return VK_DELETE;
		case SDLK_HOME: return VK_HOME;
		case SDLK_END: return VK_END;
		case SDLK_PAGEUP: return VK_PRIOR;
		case SDLK_PAGEDOWN: return VK_NEXT;
		case SDLK_LSHIFT: case SDLK_RSHIFT: return VK_SHIFT;
		case SDLK_LCTRL: case SDLK_RCTRL: return VK_CONTROL;
		case SDLK_LALT: case SDLK_RALT: return VK_MENU;
		case SDLK_CAPSLOCK: return VK_CAPITAL;
		case SDLK_NUMLOCKCLEAR: return VK_NUMLOCK;
		case SDLK_SCROLLLOCK: return VK_SCROLL;
		case SDLK_PAUSE: return VK_PAUSE;
		case SDLK_PRINTSCREEN: return VK_SNAPSHOT;
		case SDLK_KP_0: return VK_NUMPAD0;
		case SDLK_KP_MULTIPLY: return VK_MULTIPLY;
		case SDLK_KP_PLUS: return VK_ADD;
		case SDLK_KP_MINUS: return VK_SUBTRACT;
		case SDLK_KP_PERIOD: return VK_DECIMAL;
		case SDLK_KP_DIVIDE: return VK_DIVIDE;
		case SDLK_SEMICOLON: return 0xBA;
		case SDLK_EQUALS: return 0xBB;
		case SDLK_COMMA: return 0xBC;
		case SDLK_MINUS: return 0xBD;
		case SDLK_PERIOD: return 0xBE;
		case SDLK_SLASH: return 0xBF;
		case SDLK_BACKQUOTE: return 0xC0;
		case SDLK_LEFTBRACKET: return 0xDB;
		case SDLK_BACKSLASH: return 0xDC;
		case SDLK_RIGHTBRACKET: return 0xDD;
		case SDLK_QUOTE: return 0xDE;
	}
	return 0;
}

/*
** US-layout VkKeyScan: VK code in the low byte, shift state (0x100) above.
** KEYBOARD.CPP builds its VK<->ASCII tables from this.
*/
extern "C" SHORT WINAPI VkKeyScan(CHAR ch)
{
	static const char shifted_digits[] = ")!@#$%^&*(";
	static const struct { char plain, shift; unsigned char vk; } punct[] = {
		{'-', '_', 0xBD}, {'=', '+', 0xBB}, {'[', '{', 0xDB}, {']', '}', 0xDD},
		{'\\', '|', 0xDC}, {';', ':', 0xBA}, {'\'', '"', 0xDE}, {'`', '~', 0xC0},
		{',', '<', 0xBC}, {'.', '>', 0xBE}, {'/', '?', 0xBF},
	};
	unsigned char c = (unsigned char)ch;
	if (c >= 'a' && c <= 'z') return (SHORT)(c - 'a' + 'A');
	if (c >= 'A' && c <= 'Z') return (SHORT)(c | 0x100);
	if (c >= '0' && c <= '9') return (SHORT)c;
	if (c == ' ') return VK_SPACE;
	for (int i = 0; i < 10; i++) if (c == (unsigned char)shifted_digits[i]) return (SHORT)(('0' + i) | 0x100);
	for (size_t i = 0; i < sizeof(punct) / sizeof(punct[0]); i++) {
		if (c == (unsigned char)punct[i].plain) return punct[i].vk;
		if (c == (unsigned char)punct[i].shift) return (SHORT)(punct[i].vk | 0x100);
	}
	return -1;
}

/*
** Mouse button state as the game has been told it (not SDL's live state):
** the button-down message and "is it still down?" must agree even when a
** click is shorter than a game frame.
*/
static bool ButtonDown[3];		// left, right, middle

static int Mouse_Buttons_To_VK_Down(int vk)
{
	if (vk == VK_LBUTTON) return ButtonDown[0];
	if (vk == VK_RBUTTON) return ButtonDown[1];
	if (vk == VK_MBUTTON) return ButtonDown[2];
	return 0;
}

extern "C" short WINAPI GetKeyState(int vk)
{
	vk &= 0xFF;
	short state = 0;
	if (KeyDown[vk] || Mouse_Buttons_To_VK_Down(vk)) state |= (short)0x8000;
#ifndef WEBCANDC_HEADLESS
	SDL_Keymod mod = SDL_GetModState();
	if (vk == VK_CAPITAL && (mod & KMOD_CAPS)) state |= 1;
	if (vk == VK_NUMLOCK && (mod & KMOD_NUM)) state |= 1;
#endif
	return state;
}

extern "C" short WINAPI GetAsyncKeyState(int vk)
{
	WebCandC_Service();
	vk &= 0xFF;
	return (KeyDown[vk] || Mouse_Buttons_To_VK_Down(vk)) ? (short)0x8000 : 0;
}

extern "C" BOOL WINAPI GetKeyboardState(PBYTE state)
{
	for (int i = 0; i < 256; i++) state[i] = (BYTE)((GetKeyState(i) & 0x8000) ? 0x80 : 0);
	return TRUE;
}

extern "C" UINT WINAPI MapVirtualKey(UINT code, UINT) { return code; }
extern "C" int WINAPI ToAscii(UINT vk, UINT, const BYTE *, LPWORD out, UINT)
{
	if (vk >= 'A' && vk <= 'Z') { *out = (WORD)(vk - 'A' + 'a'); return 1; }
	if (vk >= '0' && vk <= '9') { *out = (WORD)vk; return 1; }
	return 0;
}

/*
** ---- Mouse ----------------------------------------------------------------
*/
static int MouseX, MouseY;		// In game (logical) coordinates.
static int CursorShowCount = 0;

extern "C" BOOL WINAPI GetCursorPos(LPPOINT pt)
{
	pt->x = MouseX;
	pt->y = MouseY;
	return TRUE;
}

extern "C" BOOL WINAPI SetCursorPos(int x, int y)
{
	MouseX = x;
	MouseY = y;
	return TRUE;
}

extern "C" int WINAPI ShowCursor(BOOL show)
{
	CursorShowCount += show ? 1 : -1;
	return CursorShowCount;
}

extern "C" HCURSOR WINAPI SetCursor(HCURSOR c) { return c; }
extern "C" HCURSOR WINAPI LoadCursor(HINSTANCE, LPCSTR) { return NULL; }
extern "C" BOOL WINAPI ClipCursor(const RECT *) { return TRUE; }
extern "C" HWND WINAPI SetCapture(HWND w) { return w; }
extern "C" BOOL WINAPI ReleaseCapture(void) { return TRUE; }
extern "C" BOOL WINAPI ScreenToClient(HWND, LPPOINT) { return TRUE; }
extern "C" BOOL WINAPI ClientToScreen(HWND, LPPOINT) { return TRUE; }

/*
** ---- Windows and messages -------------------------------------------------
*/
static WNDPROC MainProc = NULL;
static HWND const MainHwnd = (HWND)0x1000;
static std::deque<MSG> Queue;
static bool QuitPosted = false;
static UINT NextRegisteredMessage = 0xC000;

static void Post(UINT message, WPARAM w, LPARAM l)
{
	MSG m;
	memset(&m, 0, sizeof(m));
	m.hwnd = MainHwnd;
	m.message = message;
	m.wParam = w;
	m.lParam = l;
	m.time = (DWORD)Now_Ms();
	m.pt.x = MouseX;
	m.pt.y = MouseY;
	Queue.push_back(m);
}

static void Queue_Button(int b, bool down, int x, int y);

static void Translate_SDL_Event(SDL_Event const &e)
{
	switch (e.type) {
		case SDL_KEYDOWN:
		case SDL_KEYUP: {
			int vk = SDL_To_VK(e.key.keysym.sym);
			if (!vk) break;
			bool down = (e.type == SDL_KEYDOWN);
			KeyDown[vk] = down;
			bool alt = (SDL_GetModState() & KMOD_ALT) != 0;
			UINT msg = down ? (alt ? WM_SYSKEYDOWN : WM_KEYDOWN) : (alt ? WM_SYSKEYUP : WM_KEYUP);
			LPARAM l = 1 | ((LPARAM)e.key.keysym.scancode << 16) | (down ? 0 : (LPARAM)0xC0000000);
			Post(msg, (WPARAM)vk, l);
			break;
		}
		case SDL_MOUSEMOTION:
			MouseX = e.motion.x;
			MouseY = e.motion.y;
			Post(WM_MOUSEMOVE, 0, MAKELONG(MouseX, MouseY));
			break;
		case SDL_MOUSEBUTTONDOWN:
		case SDL_MOUSEBUTTONUP: {
			int b;
			if (e.button.button == SDL_BUTTON_LEFT) b = 0;
			else if (e.button.button == SDL_BUTTON_RIGHT) b = 1;
			else if (e.button.button == SDL_BUTTON_MIDDLE) b = 2;
			else break;
			Queue_Button(b, e.type == SDL_MOUSEBUTTONDOWN, e.button.x, e.button.y);
			break;
		}
		default:
			break;
	}
}

#ifdef WEBCANDC_HEADLESS
/*
** Test harness input: $WEBCANDC_SCRIPT names a file of timed events, one per
** line, time in milliseconds since start:
**     3000 key RETURN        press and release a key (A-Z, 0-9, or a name below)
**     5000 click 320 200     left click at game coordinates
**     5200 rclick 320 200    right click
**     6000 move 100 100      move the mouse
**     9000 exit              end the run
*/
extern "C" char *webcandc_read_script(void);	// library_webcandc.js

struct ScriptEvent {
	double Time;
	UINT Message;
	WPARAM WParam;
	int X, Y;
	bool Exit;
};
static std::vector<ScriptEvent> Script;
static size_t ScriptPos = 0;
static bool ScriptLoaded = false;

static int Script_Key(char const *name)
{
	static const struct { char const *Name; int VK; } names[] = {
		{"RETURN", VK_RETURN}, {"ENTER", VK_RETURN}, {"ESCAPE", VK_ESCAPE}, {"ESC", VK_ESCAPE},
		{"SPACE", VK_SPACE}, {"TAB", VK_TAB}, {"BACK", VK_BACK}, {"LEFT", VK_LEFT}, {"RIGHT", VK_RIGHT},
		{"UP", VK_UP}, {"DOWN", VK_DOWN}, {"HOME", VK_HOME}, {"END", VK_END}, {"SHIFT", VK_SHIFT},
		{"CTRL", VK_CONTROL}, {"ALT", VK_MENU},
	};
	if (name[0] && !name[1]) return toupper((unsigned char)name[0]);
	if (name[0] == 'F' && name[1] >= '1' && name[1] <= '9') return VK_F1 + atoi(name + 1) - 1;
	for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) if (!strcmp(names[i].Name, name)) return names[i].VK;
	return 0;
}

static void Load_Script(void)
{
	ScriptLoaded = true;
	char *text = webcandc_read_script();
	if (!text) return;
	for (char *line = strtok(text, "\n"); line; line = strtok(NULL, "\n")) {
		double t;
		char action[16], arg[32];
		int x = 0, y = 0;
		if (sscanf(line, "%lf %15s", &t, action) < 2) continue;
		ScriptEvent e = {t, 0, 0, 0, 0, false};
		if (!strcmp(action, "key") && sscanf(line, "%*f %*s %31s", arg) == 1) {
			int vk = Script_Key(arg);
			e.Message = WM_KEYDOWN; e.WParam = vk; Script.push_back(e);
			e.Time += 80; e.Message = WM_KEYUP; Script.push_back(e);
		} else if ((!strcmp(action, "click") || !strcmp(action, "rclick") || !strcmp(action, "move")) && sscanf(line, "%*f %*s %d %d", &x, &y) == 2) {
			e.X = x; e.Y = y;
			e.Message = WM_MOUSEMOVE; Script.push_back(e);
			if (strcmp(action, "move")) {
				bool right = action[0] == 'r';
				e.Time += 30; e.Message = right ? WM_RBUTTONDOWN : WM_LBUTTONDOWN; Script.push_back(e);
				e.Time += 80; e.Message = right ? WM_RBUTTONUP : WM_LBUTTONUP; Script.push_back(e);
			}
		} else if (!strcmp(action, "hold") && sscanf(line, "%*f %*s %31s %d", arg, &x) == 2) {
			int vk = Script_Key(arg);
			e.Message = WM_KEYDOWN; e.WParam = vk; Script.push_back(e);
			e.Time += x; e.Message = WM_KEYUP; Script.push_back(e);
		} else if (!strcmp(action, "exit")) {
			e.Exit = true; Script.push_back(e);
		}
	}
	free(text);
	fprintf(stderr, "[script] %zu events\n", Script.size());
}

static void Post(UINT message, WPARAM w, LPARAM l);

static void Run_Script(void)
{
	if (!ScriptLoaded) Load_Script();
	double now = Now_Ms();
	while (ScriptPos < Script.size() && Script[ScriptPos].Time <= now) {
		ScriptEvent const &e = Script[ScriptPos++];
		if (e.Exit) {
			fprintf(stderr, "[script] exit at %.1fs\n", now / 1000.0);
			emscripten_force_exit(0);
		}
		if (e.Message == WM_KEYDOWN || e.Message == WM_KEYUP) {
			KeyDown[e.WParam & 0xFF] = (e.Message == WM_KEYDOWN);
			Post(e.Message, e.WParam, e.Message == WM_KEYUP ? (LPARAM)0xC0000001 : 1);
		} else {
			MouseX = e.X;
			MouseY = e.Y;
			if (e.Message == WM_LBUTTONDOWN || e.Message == WM_LBUTTONUP) ButtonDown[0] = (e.Message == WM_LBUTTONDOWN);
			if (e.Message == WM_RBUTTONDOWN || e.Message == WM_RBUTTONUP) ButtonDown[1] = (e.Message == WM_RBUTTONDOWN);
			Post(e.Message, 0, MAKELONG(e.X, e.Y));
		}
	}
}
#endif

/*
** A press is always delivered at once. A release that follows its press
** within MIN_CLICK_MS is held back until the press has been visible that
** long: the game notices a press on its next frame and then asks whether the
** button is still down (Westwood gadgets act on press-then-release), which a
** click shorter than a frame -- a trackpad tap -- would otherwise fail.
*/
#define MIN_CLICK_MS 60.0
struct PendingButton { int Button; bool Down; int X, Y; double Due; };
static std::deque<PendingButton> PendingButtons;
static double LastPress[3] = { -1e9, -1e9, -1e9 };

static void Deliver_Button(int b, bool down, int x, int y)
{
	static UINT const msgs[3][2] = {
		{ WM_LBUTTONUP, WM_LBUTTONDOWN }, { WM_RBUTTONUP, WM_RBUTTONDOWN }, { WM_MBUTTONUP, WM_MBUTTONDOWN }
	};
	ButtonDown[b] = down;
	MouseX = x;
	MouseY = y;
	Post(msgs[b][down ? 1 : 0], 0, MAKELONG(x, y));
	if (down) LastPress[b] = Now_Ms();
}

static void Queue_Button(int b, bool down, int x, int y)
{
	/*
	** A release whose press the game never saw -- the end of the click on the
	** page's "play" button, say -- would reach whatever gadget lies under it.
	*/
	bool press_pending = false;
	for (size_t i = 0; i < PendingButtons.size(); i++) {
		if (PendingButtons[i].Button == b) press_pending = PendingButtons[i].Down;
	}
	if (!down && !ButtonDown[b] && !press_pending) return;

	double due = Now_Ms();
	if (!down) due = LastPress[b] + MIN_CLICK_MS;
	if (PendingButtons.empty() && due <= Now_Ms()) {
		Deliver_Button(b, down, x, y);
	} else {
		PendingButton p = { b, down, x, y, due };
		PendingButtons.push_back(p);
	}
}

static void Release_Pending_Buttons(void)
{
	while (!PendingButtons.empty()) {
		PendingButton p = PendingButtons.front();
		double due = p.Down ? 0 : LastPress[p.Button] + MIN_CLICK_MS;
		if (due > Now_Ms()) break;
		PendingButtons.pop_front();
		Deliver_Button(p.Button, p.Down, p.X, p.Y);
	}
}

static void Pump_SDL(void)
{
	Release_Pending_Buttons();
#ifdef WEBCANDC_HEADLESS
	Run_Script();
	return;
#endif
	SDL_Event e;
	while (SDL_PollEvent(&e)) {
		Translate_SDL_Event(e);
	}
}

extern "C" ATOM WINAPI RegisterClass(const WNDCLASS *wc)
{
	MainProc = wc->lpfnWndProc;
	return 1;
}

extern "C" HWND WINAPI CreateWindowEx(DWORD, LPCSTR, LPCSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID)
{
	/*
	** Windows activates a freshly shown top-level window; the game relies on
	** WM_ACTIVATEAPP to set GameInFocus, without which every surface Lock fails.
	*/
	Post(WM_ACTIVATEAPP, TRUE, 0);
	return MainHwnd;
}

extern "C" HWND WINAPI CreateWindow(LPCSTR cls, LPCSTR name, DWORD style, int x, int y, int w, int h, HWND parent, HMENU menu, HINSTANCE inst, LPVOID param)
{
	return CreateWindowEx(0, cls, name, style, x, y, w, h, parent, menu, inst, param);
}

extern "C" BOOL WINAPI PeekMessage(LPMSG msg, HWND, UINT, UINT, UINT remove)
{
	WebCandC_Service();
	if (Queue.empty()) return FALSE;
	*msg = Queue.front();
	if (remove & PM_REMOVE) Queue.pop_front();
	return TRUE;
}

extern "C" BOOL WINAPI GetMessage(LPMSG msg, HWND, UINT, UINT)
{
	WebCandC_Service();
	while (Queue.empty()) {
		if (QuitPosted) {
			memset(msg, 0, sizeof(*msg));
			msg->message = WM_QUIT;
			return FALSE;
		}
		WebCandC_Yield();
	}
	*msg = Queue.front();
	Queue.pop_front();
	return msg->message != WM_QUIT;
}

extern "C" BOOL WINAPI TranslateMessage(const MSG *) { return FALSE; }

extern "C" LRESULT WINAPI DispatchMessage(const MSG *msg)
{
	if (MainProc) return MainProc(msg->hwnd, msg->message, msg->wParam, msg->lParam);
	return 0;
}

extern "C" BOOL WINAPI PostMessage(HWND, UINT msg, WPARAM w, LPARAM l)
{
	Post(msg, w, l);
	return TRUE;
}

extern "C" LRESULT WINAPI SendMessage(HWND hwnd, UINT msg, WPARAM w, LPARAM l)
{
	if (MainProc) return MainProc(hwnd ? hwnd : MainHwnd, msg, w, l);
	return 0;
}

extern "C" void WINAPI PostQuitMessage(int code)
{
	QuitPosted = true;
	Post(WM_QUIT, (WPARAM)code, 0);
}

extern "C" LRESULT WINAPI DefWindowProc(HWND, UINT, WPARAM, LPARAM) { return 0; }
extern "C" UINT WINAPI RegisterWindowMessage(LPCSTR) { return NextRegisteredMessage++; }
extern "C" BOOL WINAPI DestroyWindow(HWND) { return TRUE; }
extern "C" BOOL WINAPI ShowWindow(HWND, int) { return TRUE; }
extern "C" BOOL WINAPI UpdateWindow(HWND) { return TRUE; }
extern "C" HWND WINAPI SetFocus(HWND w) { return w; }
extern "C" HWND WINAPI GetFocus(void) { return MainHwnd; }
extern "C" HWND WINAPI GetActiveWindow(void) { return MainHwnd; }
extern "C" HWND WINAPI SetActiveWindow(HWND w) { return w; }
extern "C" BOOL WINAPI SetForegroundWindow(HWND) { return TRUE; }
extern "C" HWND WINAPI GetForegroundWindow(void) { return MainHwnd; }
extern "C" HWND WINAPI FindWindow(LPCSTR, LPCSTR) { return NULL; }
extern "C" BOOL WINAPI IsWindow(HWND w) { return w == MainHwnd; }
extern "C" BOOL WINAPI IsIconic(HWND) { return FALSE; }
extern "C" BOOL WINAPI InvalidateRect(HWND, const RECT *, BOOL) { return TRUE; }
extern "C" BOOL WINAPI GetClientRect(HWND, LPRECT r) { r->left = r->top = 0; r->right = 640; r->bottom = 400; return TRUE; }
extern "C" BOOL WINAPI GetWindowRect(HWND w, LPRECT r) { return GetClientRect(w, r); }
extern "C" BOOL WINAPI MoveWindow(HWND, int, int, int, int, BOOL) { return TRUE; }
extern "C" HDC WINAPI BeginPaint(HWND, LPPAINTSTRUCT ps) { memset(ps, 0, sizeof(*ps)); return NULL; }
extern "C" BOOL WINAPI EndPaint(HWND, const PAINTSTRUCT *) { return TRUE; }
extern "C" HDC WINAPI GetDC(HWND) { return NULL; }
extern "C" int WINAPI ReleaseDC(HWND, HDC) { return 1; }
extern "C" UINT WINAPI SetTimer(HWND, UINT id, UINT, TIMERPROC) { return id; }
extern "C" BOOL WINAPI KillTimer(HWND, UINT) { return TRUE; }
extern "C" int WINAPI GetSystemMetrics(int index) { return index == SM_CXSCREEN ? 640 : 400; }
extern "C" HICON WINAPI LoadIcon(HINSTANCE, LPCSTR) { return NULL; }
extern "C" HGDIOBJ WINAPI GetStockObject(int) { return NULL; }
extern "C" int WINAPI DialogBox(HINSTANCE, LPCSTR, HWND, DLGPROC) { return IDCANCEL; }
extern "C" BOOL WINAPI EndDialog(HWND, int) { return TRUE; }
extern "C" HWND WINAPI GetDlgItem(HWND, int) { return NULL; }
extern "C" BOOL WINAPI SetDlgItemText(HWND, int, LPCSTR) { return TRUE; }
extern "C" LRESULT WINAPI SendDlgItemMessage(HWND, int, UINT, WPARAM, LPARAM) { return 0; }
extern "C" BOOL WINAPI SetWindowText(HWND, LPCSTR) { return TRUE; }

extern "C" int WINAPI MessageBox(HWND, LPCSTR text, LPCSTR caption, UINT type)
{
	fprintf(stderr, "[MessageBox] %s: %s\n", caption ? caption : "", text ? text : "");
	if ((type & 0xF) == MB_YESNO || (type & 0xF) == MB_YESNOCANCEL) return IDYES;
	return IDOK;
}

/*
** ---- Process, modules, threads, sync --------------------------------------
*/
static DWORD LastError = 0;

extern "C" HMODULE WINAPI GetModuleHandle(LPCSTR) { return (HMODULE)1; }

extern "C" DWORD WINAPI GetModuleFileName(HMODULE, LPSTR buf, DWORD size)
{
	snprintf(buf, size, "/data/C&C95.EXE");
	return (DWORD)strlen(buf);
}

extern "C" DWORD WINAPI GetLastError(void) { return LastError; }
extern "C" void WINAPI SetLastError(DWORD err) { LastError = err; }
extern "C" void WINAPI OutputDebugString(LPCSTR text) { fputs(text, stderr); }

extern "C" void WINAPI ExitProcess(UINT code)
{
	fprintf(stderr, "[webcandc] ExitProcess(%u)\n", code);
	emscripten_force_exit((int)code);
}

extern "C" HANDLE WINAPI GetCurrentProcess(void) { return (HANDLE)1; }
extern "C" HANDLE WINAPI GetCurrentThread(void) { return (HANDLE)1; }
extern "C" DWORD WINAPI GetCurrentThreadId(void) { return 1; }
extern "C" BOOL WINAPI SetPriorityClass(HANDLE, DWORD) { return TRUE; }
extern "C" DWORD WINAPI GetPriorityClass(HANDLE) { return 0x00000020; }	// NORMAL_PRIORITY_CLASS
extern "C" BOOL WINAPI SetThreadPriority(HANDLE, int) { return TRUE; }

/*
** There are no threads. The one worker the game starts (a background file
** reader in CONQUER.CPP) is simply run to completion before CreateThread
** returns, which the caller's wait loop then observes as already finished.
*/
extern "C" HANDLE WINAPI CreateThread(LPSECURITY_ATTRIBUTES, DWORD, LPTHREAD_START_ROUTINE start, LPVOID param, DWORD, LPDWORD id)
{
	if (id) *id = 2;
	start(param);
	return (HANDLE)2;
}

extern "C" HANDLE WINAPI CreateEvent(LPSECURITY_ATTRIBUTES, BOOL, BOOL, LPCSTR) { return (HANDLE)3; }
extern "C" BOOL WINAPI SetEvent(HANDLE) { return TRUE; }
extern "C" BOOL WINAPI ResetEvent(HANDLE) { return TRUE; }
extern "C" HANDLE WINAPI CreateMutex(LPSECURITY_ATTRIBUTES, BOOL, LPCSTR) { return (HANDLE)4; }
extern "C" BOOL WINAPI ReleaseMutex(HANDLE) { return TRUE; }
extern "C" DWORD WINAPI WaitForSingleObject(HANDLE, DWORD) { return WAIT_OBJECT_0; }
extern "C" BOOL WINAPI CloseHandle(HANDLE) { return TRUE; }
extern "C" void WINAPI InitializeCriticalSection(LPCRITICAL_SECTION) {}
extern "C" void WINAPI DeleteCriticalSection(LPCRITICAL_SECTION) {}
extern "C" void WINAPI EnterCriticalSection(LPCRITICAL_SECTION) {}
extern "C" void WINAPI LeaveCriticalSection(LPCRITICAL_SECTION) {}

extern "C" BOOL WINAPI GetVersionEx(LPOSVERSIONINFO info)
{
	info->dwMajorVersion = 4;		// Windows 95
	info->dwMinorVersion = 0;
	info->dwBuildNumber = 950;
	info->dwPlatformId = 1;			// VER_PLATFORM_WIN32_WINDOWS
	info->szCSDVersion[0] = '\0';
	return TRUE;
}

extern "C" void WINAPI GlobalMemoryStatus(LPMEMORYSTATUS status)
{
	status->dwMemoryLoad = 10;
	status->dwTotalPhys = 64u * 1024 * 1024;
	status->dwAvailPhys = 48u * 1024 * 1024;
	status->dwTotalPageFile = 128u * 1024 * 1024;
	status->dwAvailPageFile = 96u * 1024 * 1024;
	status->dwTotalVirtual = 256u * 1024 * 1024;
	status->dwAvailVirtual = 192u * 1024 * 1024;
}

extern "C" UINT WINAPI WinExec(LPCSTR, UINT) { return 0; }
extern "C" HINSTANCE WINAPI ShellExecute(HWND, LPCSTR, LPCSTR, LPCSTR, LPCSTR, int) { return NULL; }
extern "C" HMODULE WINAPI LoadLibrary(LPCSTR) { return NULL; }
extern "C" BOOL WINAPI FreeLibrary(HMODULE) { return TRUE; }
extern "C" FARPROC WINAPI GetProcAddress(HMODULE, LPCSTR) { return NULL; }
extern "C" BOOL WINAPI IsBadReadPtr(const void *ptr, UINT) { return ptr == NULL; }
extern "C" BOOL WINAPI IsBadWritePtr(void *ptr, UINT) { return ptr == NULL; }

/*
** ---- Memory -----------------------------------------------------------------
*/
extern "C" HGLOBAL WINAPI GlobalAlloc(UINT flags, DWORD bytes)
{
	return (flags & GMEM_ZEROINIT) ? calloc(1, bytes ? bytes : 1) : malloc(bytes ? bytes : 1);
}
extern "C" HGLOBAL WINAPI GlobalFree(HGLOBAL mem) { free(mem); return NULL; }
extern "C" LPVOID WINAPI GlobalLock(HGLOBAL mem) { return mem; }
extern "C" BOOL WINAPI GlobalUnlock(HGLOBAL) { return TRUE; }
extern "C" DWORD WINAPI GlobalSize(HGLOBAL) { return 0; }
extern "C" HGLOBAL WINAPI GlobalReAlloc(HGLOBAL mem, DWORD bytes, UINT) { return realloc(mem, bytes); }
extern "C" HLOCAL WINAPI LocalAlloc(UINT flags, UINT bytes) { return GlobalAlloc(flags, bytes); }
extern "C" HLOCAL WINAPI LocalFree(HLOCAL mem) { free(mem); return NULL; }
extern "C" LPVOID WINAPI VirtualAlloc(LPVOID, DWORD size, DWORD, DWORD) { return calloc(1, size); }
extern "C" BOOL WINAPI VirtualFree(LPVOID addr, DWORD, DWORD) { free(addr); return TRUE; }
extern "C" BOOL WINAPI VirtualLock(LPVOID, DWORD) { return TRUE; }
extern "C" BOOL WINAPI VirtualUnlock(LPVOID, DWORD) { return TRUE; }

/*
** ---- Files ----------------------------------------------------------------
** The game does its real file I/O through RawFileClass (POSIX). These cover
** the few direct Win32 file calls (CD detection, file times).
*/
extern "C" HANDLE WINAPI CreateFile(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE) { return INVALID_HANDLE_VALUE; }
extern "C" BOOL WINAPI ReadFile(HANDLE, LPVOID, DWORD, LPDWORD read, LPOVERLAPPED) { if (read) *read = 0; return FALSE; }
extern "C" BOOL WINAPI WriteFile(HANDLE, LPCVOID, DWORD n, LPDWORD written, LPOVERLAPPED) { if (written) *written = n; return TRUE; }
extern "C" DWORD WINAPI SetFilePointer(HANDLE, LONG, LPLONG, DWORD) { return 0; }
extern "C" DWORD WINAPI GetFileSize(HANDLE, LPDWORD) { return 0; }
extern "C" BOOL WINAPI GetFileTime(HANDLE, LPFILETIME, LPFILETIME, LPFILETIME) { return FALSE; }
extern "C" BOOL WINAPI SetFileTime(HANDLE, const FILETIME *, const FILETIME *, const FILETIME *) { return FALSE; }
extern "C" BOOL WINAPI DeleteFile(LPCSTR name) { return unlink(name) == 0; }
extern "C" BOOL WINAPI CopyFile(LPCSTR, LPCSTR, BOOL) { return FALSE; }
extern "C" BOOL WINAPI MoveFile(LPCSTR from, LPCSTR to) { return rename(from, to) == 0; }
extern "C" DWORD WINAPI GetFileAttributes(LPCSTR name)
{
	struct stat st;
	if (stat(name, &st) != 0) return 0xFFFFFFFF;
	return S_ISDIR(st.st_mode) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
}
extern "C" BOOL WINAPI SetFileAttributes(LPCSTR, DWORD) { return TRUE; }
extern "C" HANDLE WINAPI FindFirstFile(LPCSTR, LPWIN32_FIND_DATA) { return INVALID_HANDLE_VALUE; }
extern "C" BOOL WINAPI FindNextFile(HANDLE, LPWIN32_FIND_DATA) { return FALSE; }
extern "C" BOOL WINAPI FindClose(HANDLE) { return TRUE; }
extern "C" DWORD WINAPI GetCurrentDirectory(DWORD size, LPSTR buf) { return getcwd(buf, size) ? (DWORD)strlen(buf) : 0; }
extern "C" BOOL WINAPI SetCurrentDirectory(LPCSTR dir) { return chdir(dir) == 0; }
extern "C" BOOL WINAPI CreateDirectory(LPCSTR dir, LPSECURITY_ATTRIBUTES) { return mkdir(dir, 0777) == 0; }
extern "C" UINT WINAPI GetDriveType(LPCSTR) { return DRIVE_FIXED; }
extern "C" DWORD WINAPI GetLogicalDrives(void) { return 1u << 2; }	// C:
extern "C" BOOL WINAPI GetVolumeInformation(LPCSTR, LPSTR vol, DWORD volsize, LPDWORD serial, LPDWORD maxlen, LPDWORD flags, LPSTR fs, DWORD fssize)
{
	if (vol && volsize) vol[0] = '\0';
	if (serial) *serial = 0;
	if (maxlen) *maxlen = 255;
	if (flags) *flags = 0;
	if (fs && fssize) fs[0] = '\0';
	return FALSE;
}
extern "C" BOOL WINAPI GetDiskFreeSpace(LPCSTR, LPDWORD spc, LPDWORD bps, LPDWORD freec, LPDWORD total)
{
	if (spc) *spc = 8;
	if (bps) *bps = 512;
	if (freec) *freec = 100000;
	if (total) *total = 200000;
	return TRUE;
}
extern "C" UINT WINAPI GetWindowsDirectory(LPSTR buf, UINT size) { snprintf(buf, size, "/data"); return (UINT)strlen(buf); }
extern "C" UINT WINAPI GetSystemDirectory(LPSTR buf, UINT size) { snprintf(buf, size, "/data"); return (UINT)strlen(buf); }
extern "C" DWORD WINAPI GetPrivateProfileString(LPCSTR, LPCSTR, LPCSTR def, LPSTR buf, DWORD size, LPCSTR)
{
	snprintf(buf, size, "%s", def ? def : "");
	return (DWORD)strlen(buf);
}
extern "C" UINT WINAPI GetPrivateProfileInt(LPCSTR, LPCSTR, int def, LPCSTR) { return (UINT)def; }
extern "C" BOOL WINAPI WritePrivateProfileString(LPCSTR, LPCSTR, LPCSTR, LPCSTR) { return TRUE; }
extern "C" HFILE WINAPI _lopen(LPCSTR, int) { return HFILE_ERROR; }
extern "C" HFILE WINAPI _lclose(HFILE) { return 0; }
extern "C" UINT WINAPI _lread(HFILE, LPVOID, UINT) { return 0; }
extern "C" LONG WINAPI _llseek(HFILE, LONG, int) { return 0; }

/*
** ---- Registry (none) --------------------------------------------------------
*/
extern "C" LONG WINAPI RegOpenKeyEx(HKEY, LPCSTR, DWORD, DWORD, PHKEY) { return ERROR_FILE_NOT_FOUND; }
extern "C" LONG WINAPI RegOpenKey(HKEY, LPCSTR, PHKEY) { return ERROR_FILE_NOT_FOUND; }
extern "C" LONG WINAPI RegCreateKeyEx(HKEY, LPCSTR, DWORD, LPSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, PHKEY, LPDWORD) { return ERROR_ACCESS_DENIED; }
extern "C" LONG WINAPI RegCreateKey(HKEY, LPCSTR, PHKEY) { return ERROR_ACCESS_DENIED; }
extern "C" LONG WINAPI RegQueryValueEx(HKEY, LPCSTR, LPDWORD, LPDWORD, LPBYTE, LPDWORD) { return ERROR_FILE_NOT_FOUND; }
extern "C" LONG WINAPI RegSetValueEx(HKEY, LPCSTR, DWORD, DWORD, const BYTE *, DWORD) { return ERROR_ACCESS_DENIED; }
extern "C" LONG WINAPI RegCloseKey(HKEY) { return ERROR_SUCCESS; }

/*
** ---- GDI (unused paths) -----------------------------------------------------
*/
extern "C" UINT WINAPI GetSystemPaletteEntries(HDC, UINT, UINT, LPPALETTEENTRY) { return 0; }
extern "C" HPALETTE WINAPI CreatePalette(const LOGPALETTE *) { return NULL; }
extern "C" BOOL WINAPI DeleteObject(HGDIOBJ) { return TRUE; }
extern "C" HGDIOBJ WINAPI SelectObject(HDC, HGDIOBJ) { return NULL; }
extern "C" int WINAPI GetDeviceCaps(HDC, int) { return 0; }

extern "C" HRESULT WINAPI CoInitialize(LPVOID) { return S_OK; }
extern "C" void WINAPI CoUninitialize(void) {}
extern "C" HRESULT WINAPI CoCreateInstance(REFCLSID, LPUNKNOWN, DWORD, REFIID, LPVOID *ppv) { if (ppv) *ppv = NULL; return E_NOINTERFACE; }

/*
** ---- The service loop ---------------------------------------------------------
*/
static double LastYield = 0;
static int ServiceDepth = 0;

extern "C" void WebCandC_Service(void)
{
	/*
	** Timer callbacks (the 60 Hz tick, the mouse) may read the clock or lock
	** the screen themselves; don't recurse into them.
	*/
	if (ServiceDepth) return;
	ServiceDepth++;
	Pump_SDL();
	Fire_Timers();
	ServiceDepth--;

	/*
	** Hand the browser a frame roughly every display refresh. Every 1995 wait
	** loop reads the clock or pumps messages, so this is reached often.
	*/
	if (Now_Ms() - LastYield >= 15.0) {
		WebCandC_Yield();
	}
}

extern "C" void WebCandC_Yield(void)
{
#ifdef WEBCANDC_HEADLESS
	/*
	** Test harness: every five seconds, show where the game is waiting.
	*/
	static double last_trace = 0;
	if (Now_Ms() - last_trace >= 5000.0) {
		last_trace = Now_Ms();
		emscripten_log(EM_LOG_CONSOLE | EM_LOG_C_STACK, "[trace] t=%.1fs", Now_Ms() / 1000.0);
	}
#endif
	WebCandC_Present();
	LastYield = Now_Ms();
	emscripten_sleep(0);
	ServiceDepth++;
	Pump_SDL();
	Fire_Timers();
	ServiceDepth--;
}

/*
** ---- Idling -------------------------------------------------------------------
** The 1995 code waits by spinning: it reads a clock until the value changes.
** On Windows that burned a CPU as well; in a browser tab it keeps a core busy
** for as long as the game is open. A clock only moves when a timer fires, so
** many reads in a row that return the same value mean the game is waiting:
** sleep until the next timer is due. A frame's logic reads the clock too, but
** seldom this often within one tick, and a sleep there costs at most one tick
** of a frame that would otherwise wait for its frame timer.
*/
#define IDLE_READS 64
static struct { unsigned Value; int Reads; } Clocks[WEBCANDC_CLOCK_COUNT];

/*
** Sleep until the next timer is due: for loops that exist only to wait but do
** too much work per pass to trip the repeated-read test below.
*/
extern "C" void WebCandC_Idle(void)
{
	double now = Now_Ms();
	double due = now + 16.0;
	for (size_t i = 0; i < Timers.size(); i++) {
		if (Timers[i].Due < due) due = Timers[i].Due;
	}
	if (ServiceDepth || due - now < 1.0) return;
	WebCandC_Present();
	LastYield = Now_Ms();
	emscripten_sleep((unsigned)(due - now));
	ServiceDepth++;
	Pump_SDL();
	Fire_Timers();
	ServiceDepth--;
}

extern "C" void WebCandC_Clock_Read(int clock, unsigned value)
{
	if (clock < 0 || clock >= WEBCANDC_CLOCK_COUNT) return;
	if (value != Clocks[clock].Value) {
		Clocks[clock].Value = value;
		Clocks[clock].Reads = 0;
		return;
	}
	if (++Clocks[clock].Reads < IDLE_READS) return;
	Clocks[clock].Reads = 0;
	WebCandC_Idle();
}
