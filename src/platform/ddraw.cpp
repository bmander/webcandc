/*
** webcandc: DirectDraw (DX5 interfaces from wwlib/DDRAW.H) on SDL2.
**
** Every surface is an 8-bit block of memory. The primary surface is what the
** player sees: whenever it changes (Unlock, Blt, palette update) it is marked
** dirty, and WebCandC_Present() expands it through the palette into an RGBA
** streaming texture. Hardware blit capabilities are deliberately not
** advertised, so the game keeps its hidden page in system memory and draws
** with its own software routines -- the same path it took on a card without
** a blitter in 1995.
*/
WEBCANDC_SYSTEM_HEADERS_BEGIN
#include <SDL2/SDL.h>
#include <emscripten.h>
WEBCANDC_SYSTEM_HEADERS_END
#include <windows.h>
#include <ddraw.h>
#include <stdio.h>
#include <string.h>

#ifndef DDSD_LPSURFACE
#define DDSD_LPSURFACE 0x00000800l	// Absent from this (DX3-era) DDRAW.H.
#endif

static SDL_Window *Window;
static SDL_Renderer *Renderer;
static SDL_Texture *Texture;
static int ModeWidth = 640, ModeHeight = 400;
static bool ScreenDirty = false;
static unsigned int *RGBABuffer;

extern "C" void webcandc_dump_frame(unsigned int const *rgba, int w, int h, int n);	// library_webcandc.js

class WCPalette;
class WCSurface;
static WCSurface *PrimarySurface;

/*
** ---- IDirectDrawPalette -----------------------------------------------------
*/
class WCPalette : public IDirectDrawPalette
{
	public:
		PALETTEENTRY Entries[256];
		ULONG Refs;

		WCPalette(LPPALETTEENTRY initial) : Refs(1)
		{
			if (initial) memcpy(Entries, initial, sizeof(Entries));
			else memset(Entries, 0, sizeof(Entries));
		}

		STDMETHOD(QueryInterface)(REFIID, LPVOID *ppv) { *ppv = NULL; return E_NOINTERFACE; }
		STDMETHOD_(ULONG, AddRef)(void) { return ++Refs; }
		STDMETHOD_(ULONG, Release)(void) { ULONG r = --Refs; if (!r) delete this; return r; }
		STDMETHOD(GetCaps)(LPDWORD caps) { *caps = DDPCAPS_8BIT | DDPCAPS_ALLOW256; return DD_OK; }

		STDMETHOD(GetEntries)(DWORD, DWORD start, DWORD count, LPPALETTEENTRY out)
		{
			if (start + count > 256) return DDERR_INVALIDPARAMS;
			memcpy(out, &Entries[start], count * sizeof(PALETTEENTRY));
			return DD_OK;
		}

		STDMETHOD(Initialize)(LPDIRECTDRAW, DWORD, LPPALETTEENTRY) { return DD_OK; }

		STDMETHOD(SetEntries)(DWORD, DWORD start, DWORD count, LPPALETTEENTRY in)
		{
			if (start + count > 256) return DDERR_INVALIDPARAMS;
			memcpy(&Entries[start], in, count * sizeof(PALETTEENTRY));
			ScreenDirty = true;
			return DD_OK;
		}
};

/*
** ---- IDirectDrawSurface -----------------------------------------------------
*/
class WCSurface : public IDirectDrawSurface
{
	public:
		DWORD Width;
		DWORD Height;
		LONG Pitch;
		unsigned char *Pixels;
		bool Primary;
		WCPalette *Palette;
		DDCOLORKEY SrcKey;
		ULONG Refs;

		WCSurface(DWORD w, DWORD h, bool primary) :
			Width(w), Height(h), Pitch((LONG)w), Primary(primary), Palette(NULL), Refs(1)
		{
			Pixels = (unsigned char *)calloc(1, (size_t)w * h);
			SrcKey.dwColorSpaceLowValue = 0;
			SrcKey.dwColorSpaceHighValue = 0;
			if (primary) PrimarySurface = this;
		}

		~WCSurface()
		{
			if (PrimarySurface == this) PrimarySurface = NULL;
			if (Palette) Palette->Release();
			free(Pixels);
		}

		void Fill_Desc(LPDDSURFACEDESC desc)
		{
			DWORD size = desc->dwSize;
			memset(desc, 0, sizeof(DDSURFACEDESC));
			desc->dwSize = size;
			desc->dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PITCH | DDSD_PIXELFORMAT;
			desc->dwWidth = Width;
			desc->dwHeight = Height;
			desc->lPitch = Pitch;
			desc->ddpfPixelFormat.dwSize = sizeof(DDPIXELFORMAT);
			desc->ddpfPixelFormat.dwFlags = DDPF_PALETTEINDEXED8 | DDPF_RGB;
			desc->ddpfPixelFormat.dwRGBBitCount = 8;
			GetCaps(&desc->ddsCaps);
		}

		STDMETHOD(QueryInterface)(REFIID, LPVOID *ppv) { *ppv = NULL; return E_NOINTERFACE; }
		STDMETHOD_(ULONG, AddRef)(void) { return ++Refs; }
		STDMETHOD_(ULONG, Release)(void) { ULONG r = --Refs; if (!r) delete this; return r; }
		STDMETHOD(AddAttachedSurface)(LPDIRECTDRAWSURFACE) { return DD_OK; }
		STDMETHOD(AddOverlayDirtyRect)(LPRECT) { return DDERR_UNSUPPORTED; }

		STDMETHOD(Blt)(LPRECT dst_rect, LPDIRECTDRAWSURFACE src_iface, LPRECT src_rect, DWORD flags, LPDDBLTFX fx)
		{
			RECT d = {0, 0, (LONG)Width, (LONG)Height};
			if (dst_rect) d = *dst_rect;
			if (d.left < 0) d.left = 0;
			if (d.top < 0) d.top = 0;
			if (d.right > (LONG)Width) d.right = Width;
			if (d.bottom > (LONG)Height) d.bottom = Height;
			int w = d.right - d.left;
			int h = d.bottom - d.top;
			if (w <= 0 || h <= 0) return DD_OK;

			if (flags & DDBLT_COLORFILL) {
				unsigned char color = fx ? (unsigned char)fx->dwFillColor : 0;
				for (int y = 0; y < h; y++) memset(Pixels + (d.top + y) * Pitch + d.left, color, w);
				if (Primary) ScreenDirty = true;
				return DD_OK;
			}

			WCSurface *src = (WCSurface *)src_iface;
			if (!src) return DDERR_INVALIDPARAMS;
			RECT s = {0, 0, (LONG)src->Width, (LONG)src->Height};
			if (src_rect) s = *src_rect;
			int sw = s.right - s.left;
			int sh = s.bottom - s.top;
			if (sw <= 0 || sh <= 0) return DD_OK;

			bool keyed = (flags & DDBLT_KEYSRC) != 0;
			unsigned char key = (unsigned char)src->SrcKey.dwColorSpaceLowValue;

			if (sw == w && sh == h && !keyed) {
				/*
				** Straight copy. memmove per row, walking bottom-up when the
				** regions overlap downwards, so self-blits behave.
				*/
				bool backwards = (src == this && s.top < d.top);
				for (int i = 0; i < h; i++) {
					int y = backwards ? (h - 1 - i) : i;
					memmove(Pixels + (d.top + y) * Pitch + d.left, src->Pixels + (s.top + y) * src->Pitch + s.left, w);
				}
			} else {
				for (int y = 0; y < h; y++) {
					int sy = s.top + (y * sh) / h;
					unsigned char *dp = Pixels + (d.top + y) * Pitch + d.left;
					unsigned char const *sp = src->Pixels + sy * src->Pitch;
					for (int x = 0; x < w; x++) {
						unsigned char c = sp[s.left + (x * sw) / w];
						if (!keyed || c != key) dp[x] = c;
					}
				}
			}
			if (Primary) ScreenDirty = true;
			return DD_OK;
		}

		STDMETHOD(BltBatch)(LPDDBLTBATCH, DWORD, DWORD) { return DDERR_UNSUPPORTED; }

		STDMETHOD(BltFast)(DWORD x, DWORD y, LPDIRECTDRAWSURFACE src, LPRECT src_rect, DWORD)
		{
			WCSurface *s = (WCSurface *)src;
			RECT sr = {0, 0, (LONG)s->Width, (LONG)s->Height};
			if (src_rect) sr = *src_rect;
			RECT dr = {(LONG)x, (LONG)y, (LONG)x + (sr.right - sr.left), (LONG)y + (sr.bottom - sr.top)};
			return Blt(&dr, src, &sr, 0, NULL);
		}

		STDMETHOD(DeleteAttachedSurface)(DWORD, LPDIRECTDRAWSURFACE) { return DD_OK; }
		STDMETHOD(EnumAttachedSurfaces)(LPVOID, LPDDENUMSURFACESCALLBACK) { return DD_OK; }
		STDMETHOD(EnumOverlayZOrders)(DWORD, LPVOID, LPDDENUMSURFACESCALLBACK) { return DD_OK; }
		STDMETHOD(Flip)(LPDIRECTDRAWSURFACE, DWORD) { return DDERR_UNSUPPORTED; }
		STDMETHOD(GetAttachedSurface)(LPDDSCAPS, LPDIRECTDRAWSURFACE *out) { *out = NULL; return DDERR_NOTFOUND; }
		STDMETHOD(GetBltStatus)(DWORD) { return DD_OK; }

		STDMETHOD(GetCaps)(LPDDSCAPS caps)
		{
			caps->dwCaps = DDSCAPS_VIDEOMEMORY | (Primary ? (DDSCAPS_PRIMARYSURFACE | DDSCAPS_VISIBLE) : DDSCAPS_OFFSCREENPLAIN);
			return DD_OK;
		}

		STDMETHOD(GetClipper)(LPDIRECTDRAWCLIPPER *out) { *out = NULL; return DDERR_NOCLIPPERATTACHED; }

		STDMETHOD(GetColorKey)(DWORD, LPDDCOLORKEY key) { *key = SrcKey; return DD_OK; }
		STDMETHOD(GetDC)(HDC *dc) { *dc = NULL; return DDERR_UNSUPPORTED; }
		STDMETHOD(GetFlipStatus)(DWORD) { return DD_OK; }
		STDMETHOD(GetOverlayPosition)(LPLONG, LPLONG) { return DDERR_UNSUPPORTED; }

		STDMETHOD(GetPalette)(LPDIRECTDRAWPALETTE *out)
		{
			*out = Palette;
			if (Palette) Palette->AddRef();
			return Palette ? DD_OK : DDERR_NOPALETTEATTACHED;
		}

		STDMETHOD(GetPixelFormat)(LPDDPIXELFORMAT pf)
		{
			memset(pf, 0, sizeof(*pf));
			pf->dwSize = sizeof(*pf);
			pf->dwFlags = DDPF_PALETTEINDEXED8 | DDPF_RGB;
			pf->dwRGBBitCount = 8;
			return DD_OK;
		}

		STDMETHOD(GetSurfaceDesc)(LPDDSURFACEDESC desc) { Fill_Desc(desc); return DD_OK; }
		STDMETHOD(Initialize)(LPDIRECTDRAW, LPDDSURFACEDESC) { return DD_OK; }
		STDMETHOD(IsLost)(void) { return DD_OK; }

		STDMETHOD(Lock)(LPRECT rect, LPDDSURFACEDESC desc, DWORD, HANDLE)
		{
			Fill_Desc(desc);
			unsigned char *p = Pixels;
			if (rect) p += rect->top * Pitch + rect->left;
			desc->lpSurface = p;
			desc->dwFlags |= DDSD_LPSURFACE;
			return DD_OK;
		}

		STDMETHOD(ReleaseDC)(HDC) { return DD_OK; }
		STDMETHOD(Restore)(void) { return DD_OK; }
		STDMETHOD(SetClipper)(LPDIRECTDRAWCLIPPER) { return DD_OK; }

		STDMETHOD(SetColorKey)(DWORD, LPDDCOLORKEY key)
		{
			if (key) SrcKey = *key;
			return DD_OK;
		}

		STDMETHOD(SetOverlayPosition)(LONG, LONG) { return DDERR_UNSUPPORTED; }

		STDMETHOD(SetPalette)(LPDIRECTDRAWPALETTE pal)
		{
			if (pal) pal->AddRef();
			if (Palette) Palette->Release();
			Palette = (WCPalette *)pal;
			if (Primary) ScreenDirty = true;
			return DD_OK;
		}

		STDMETHOD(Unlock)(LPVOID)
		{
			if (Primary) ScreenDirty = true;
			return DD_OK;
		}

		STDMETHOD(UpdateOverlay)(LPRECT, LPDIRECTDRAWSURFACE, LPRECT, DWORD, LPDDOVERLAYFX) { return DDERR_UNSUPPORTED; }
		STDMETHOD(UpdateOverlayDisplay)(DWORD) { return DDERR_UNSUPPORTED; }
		STDMETHOD(UpdateOverlayZOrder)(DWORD, LPDIRECTDRAWSURFACE) { return DDERR_UNSUPPORTED; }
};

/*
** ---- IDirectDraw ------------------------------------------------------------
*/
static void Resize_Display(int w, int h);

class WCDirectDraw : public IDirectDraw
{
	public:
		ULONG Refs;
		WCDirectDraw() : Refs(1) {}

		STDMETHOD(QueryInterface)(REFIID, LPVOID *ppv) { *ppv = NULL; return E_NOINTERFACE; }
		STDMETHOD_(ULONG, AddRef)(void) { return ++Refs; }
		STDMETHOD_(ULONG, Release)(void) { ULONG r = --Refs; if (!r) delete this; return r; }
		STDMETHOD(Compact)(void) { return DD_OK; }
		STDMETHOD(CreateClipper)(DWORD, LPDIRECTDRAWCLIPPER *out, IUnknown *) { *out = NULL; return DDERR_UNSUPPORTED; }

		STDMETHOD(CreatePalette)(DWORD, LPPALETTEENTRY entries, LPDIRECTDRAWPALETTE *out, IUnknown *)
		{
			*out = new WCPalette(entries);
			return DD_OK;
		}

		STDMETHOD(CreateSurface)(LPDDSURFACEDESC desc, LPDIRECTDRAWSURFACE *out, IUnknown *)
		{
			bool primary = (desc->ddsCaps.dwCaps & DDSCAPS_PRIMARYSURFACE) != 0;
			DWORD w = primary ? (DWORD)ModeWidth : desc->dwWidth;
			DWORD h = primary ? (DWORD)ModeHeight : desc->dwHeight;
			if (!w || !h) {
				*out = NULL;
				return DDERR_INVALIDPARAMS;
			}
			*out = new WCSurface(w, h, primary);
			return DD_OK;
		}

		STDMETHOD(DuplicateSurface)(LPDIRECTDRAWSURFACE, LPDIRECTDRAWSURFACE *out) { *out = NULL; return DDERR_UNSUPPORTED; }
		STDMETHOD(EnumDisplayModes)(DWORD, LPDDSURFACEDESC, LPVOID, LPDDENUMMODESCALLBACK) { return DD_OK; }
		STDMETHOD(EnumSurfaces)(DWORD, LPDDSURFACEDESC, LPVOID, LPDDENUMSURFACESCALLBACK) { return DD_OK; }
		STDMETHOD(FlipToGDISurface)(void) { return DD_OK; }

		STDMETHOD(GetCaps)(LPDDCAPS driver, LPDDCAPS hel)
		{
			LPDDCAPS caps[2] = {driver, hel};
			for (int i = 0; i < 2; i++) {
				if (!caps[i]) continue;
				DWORD size = caps[i]->dwSize;
				memset(caps[i], 0, size ? size : sizeof(DDCAPS));
				caps[i]->dwSize = size;
				caps[i]->dwCaps = DDCAPS_PALETTE;	// No blitter: software drawing.
				caps[i]->dwVidMemTotal = 0;
				caps[i]->dwVidMemFree = 0;
			}
			return DD_OK;
		}

		STDMETHOD(GetDisplayMode)(LPDDSURFACEDESC desc)
		{
			DWORD size = desc->dwSize;
			memset(desc, 0, sizeof(DDSURFACEDESC));
			desc->dwSize = size;
			desc->dwFlags = DDSD_WIDTH | DDSD_HEIGHT | DDSD_PITCH;
			desc->dwWidth = ModeWidth;
			desc->dwHeight = ModeHeight;
			desc->lPitch = ModeWidth;
			return DD_OK;
		}

		STDMETHOD(GetFourCCCodes)(LPDWORD n, LPDWORD) { if (n) *n = 0; return DD_OK; }
		STDMETHOD(GetGDISurface)(LPDIRECTDRAWSURFACE *out) { *out = PrimarySurface; return DD_OK; }
		STDMETHOD(GetMonitorFrequency)(LPDWORD f) { *f = 60; return DD_OK; }
		STDMETHOD(GetScanLine)(LPDWORD line) { *line = 0; return DD_OK; }
		STDMETHOD(GetVerticalBlankStatus)(LPBOOL vb) { *vb = TRUE; return DD_OK; }
		STDMETHOD(Initialize)(GUID *) { return DD_OK; }
		STDMETHOD(RestoreDisplayMode)(void) { return DD_OK; }
		STDMETHOD(SetCooperativeLevel)(HWND, DWORD) { return DD_OK; }

		STDMETHOD(SetDisplayMode)(DWORD w, DWORD h, DWORD bpp)
		{
			if (bpp != 8) return DDERR_INVALIDMODE;
			if (!((w == 640 && (h == 400 || h == 480)) || (w == 320 && h == 200))) return DDERR_INVALIDMODE;
			Resize_Display((int)w, (int)h);
			return DD_OK;
		}

		STDMETHOD(WaitForVerticalBlank)(DWORD, HANDLE) { return DD_OK; }
};

extern "C" HRESULT WINAPI DirectDrawCreate(GUID *, LPDIRECTDRAW *out, IUnknown *)
{
	*out = new WCDirectDraw();
	return DD_OK;
}

extern "C" HRESULT WINAPI DirectDrawCreateClipper(DWORD, LPDIRECTDRAWCLIPPER *out, IUnknown *)
{
	*out = NULL;
	return DDERR_UNSUPPORTED;
}

extern "C" HRESULT WINAPI DirectDrawEnumerateA(LPDDENUMCALLBACKA, LPVOID) { return DD_OK; }

/*
** ---- Presentation -------------------------------------------------------------
*/
static void Resize_Display(int w, int h)
{
	ModeWidth = w;
	ModeHeight = h;
#ifndef WEBCANDC_HEADLESS
	if (Texture) SDL_DestroyTexture(Texture);
	Texture = SDL_CreateTexture(Renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, w, h);
	SDL_RenderSetLogicalSize(Renderer, w, h);
#endif
	free(RGBABuffer);
	RGBABuffer = (unsigned int *)calloc((size_t)w * h, sizeof(unsigned int));
	ScreenDirty = true;
}

extern "C" void WebCandC_Mark_Screen_Dirty(void) { ScreenDirty = true; }

extern "C" void WebCandC_Present(void)
{
#ifdef WEBCANDC_HEADLESS
	if (!ScreenDirty || !PrimarySurface) return;
#else
	if (!ScreenDirty || !PrimarySurface || !Texture) return;
#endif
	ScreenDirty = false;

	unsigned int lut[256];
	for (int i = 0; i < 256; i++) {
		if (!PrimarySurface->Palette) { lut[i] = 0xFF000000u | (i * 0x010101u); continue; }	// no palette yet: greyscale
		PALETTEENTRY const &e = PrimarySurface->Palette->Entries[i];
		lut[i] = 0xFF000000u | ((unsigned)e.peRed << 16) | ((unsigned)e.peGreen << 8) | (unsigned)e.peBlue;
	}

	int w = (int)PrimarySurface->Width < ModeWidth ? (int)PrimarySurface->Width : ModeWidth;
	int h = (int)PrimarySurface->Height < ModeHeight ? (int)PrimarySurface->Height : ModeHeight;
	for (int y = 0; y < h; y++) {
		unsigned char const *src = PrimarySurface->Pixels + y * PrimarySurface->Pitch;
		unsigned int *dst = RGBABuffer + y * ModeWidth;
		for (int x = 0; x < w; x++) dst[x] = lut[src[x]];
	}

#ifdef WEBCANDC_HEADLESS
	/*
	** Headless (node) test harness: every WEBCANDC_FRAME_MS milliseconds of
	** wall time, write the screen to $WEBCANDC_FRAMES/frame_NNNN.ppm.
	*/
	static double last_dump = -1e9;
	static int frame_no = 0;
	double now = emscripten_get_now();
	if (now - last_dump >= 1000.0) {
		last_dump = now;
		webcandc_dump_frame(RGBABuffer, ModeWidth, ModeHeight, frame_no++);
	}
#else
	SDL_UpdateTexture(Texture, NULL, RGBABuffer, ModeWidth * 4);
	SDL_RenderClear(Renderer);
	SDL_RenderCopy(Renderer, Texture, NULL, NULL);
	SDL_RenderPresent(Renderer);
#endif
}

extern "C" void WebCandC_Init(void)
{
#ifdef WEBCANDC_HEADLESS
	Resize_Display(ModeWidth, ModeHeight);
	return;
#endif
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
	SDL_SetHint(SDL_HINT_EMSCRIPTEN_KEYBOARD_ELEMENT, "#canvas");
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_TIMER) != 0) {
		fprintf(stderr, "[webcandc] SDL_Init failed: %s\n", SDL_GetError());
		return;
	}
	Window = SDL_CreateWindow("Command & Conquer", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
		ModeWidth * 2, ModeHeight * 2, SDL_WINDOW_RESIZABLE);
	Renderer = SDL_CreateRenderer(Window, -1, 0);
	SDL_SetRenderDrawColor(Renderer, 0, 0, 0, 255);
	SDL_ShowCursor(SDL_DISABLE);
	Resize_Display(ModeWidth, ModeHeight);
}
