/*
** webcandc: DirectSound (the DirectX 3 interfaces in wwlib/DSOUND.H) on SDL2.
**
** Westwood's sound library (wwlib/SOUNDIO.CPP, SOUNDINT.CPP) plays every
** sample through a small looping secondary buffer: it Locks the stretch ahead
** of the play cursor, decompresses the next quarter-buffer of the sample into
** it, and stops the buffer once the play cursor has run past the end of the
** data. So what matters most here is an honest play cursor. Each secondary
** buffer is a ring of memory in its own PCM format; an SDL audio callback
** mixes the playing ones (resampled to the device rate, with volume and pan)
** and advances their cursors by exactly what it consumed.
**
** If the device stops asking for audio -- the tab went into the background,
** or the AudioContext is suspended -- the cursors would freeze and samples
** would never finish, hanging anything that waits on one. So once the
** callback has been silent for STALL_MS, the cursors are advanced by the wall
** clock instead, mixing into a null sink. The headless (node) build has no
** audio device and always runs that way; with $WEBCANDC_WAV set, it writes
** everything it mixes to that WAV file.
**
** SDL_LockAudioDevice guards the buffer list and play state against the
** callback. (On Emscripten the callback runs on the main thread, between
** yields, and the lock is a no-op.)
*/
WEBCANDC_SYSTEM_HEADERS_BEGIN
#include <SDL2/SDL.h>
#include <emscripten.h>
WEBCANDC_SYSTEM_HEADERS_END
#include <windows.h>
#include <dsound.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/*
** Constants of later DirectSound headers, absent from this DX3-era DSOUND.H.
*/
#ifndef DSBVOLUME_MIN
#define DSBVOLUME_MIN			-10000
#define DSBVOLUME_MAX			0
#endif
#ifndef DSBPAN_LEFT
#define DSBPAN_LEFT				-10000
#define DSBPAN_RIGHT			10000
#endif
#ifndef DSBFREQUENCY_ORIGINAL
#define DSBFREQUENCY_ORIGINAL	0
#define DSBFREQUENCY_MIN		100
#define DSBFREQUENCY_MAX		100000
#endif
#ifndef DSBSIZE_MIN
#define DSBSIZE_MIN				4
#define DSBSIZE_MAX				0x0FFFFFFF
#endif
#ifndef DSBLOCK_ENTIREBUFFER
#define DSBLOCK_ENTIREBUFFER	0x00000002
#endif

#define MIX_RATE		22050	// Mixer output: 22050 Hz stereo S16 (SDL converts to the device).
#define MIX_FRAMES		1024	// Frames per SDL callback.
#define STALL_MS		250.0	// Device silent this long: advance cursors by the clock.
#define MAX_CATCHUP_MS	1000.0	// ...but never by more than this at once.

class WCSoundBuffer;
static std::vector<WCSoundBuffer *> Secondaries;
static WCSoundBuffer *Primary;
static SDL_AudioDeviceID Device;
static double DeviceMixMs;			// Wall time of the device's last callback.
static double MixedToMs;			// Wall time up to which the cursors have advanced.
static std::vector<int> Accum;		// Mix accumulator, interleaved stereo.

static double Now_Ms(void) { return emscripten_get_now(); }

struct AudioLock {
	AudioLock() { if (Device) SDL_LockAudioDevice(Device); }
	~AudioLock() { if (Device) SDL_UnlockAudioDevice(Device); }
};

static inline Sint16 Clip16(int v)
{
	return (Sint16)(v > 32767 ? 32767 : (v < -32768 ? -32768 : v));
}

/*
** ---- WAV capture (headless harness) ------------------------------------------
*/
#ifdef WEBCANDC_HEADLESS
static int WavState = -1;	// -1 = not yet asked, 0 = off, 1 = on

static bool Wav_Enabled(void)
{
	if (WavState < 0) {
		WavState = EM_ASM_INT({ return (typeof process !== 'undefined' && process.env.WEBCANDC_WAV) ? 1 : 0; });
	}
	return WavState == 1;
}

static void Wav_Write(int const *accum, int frames)
{
	std::vector<Sint16> pcm(frames * 2);
	for (int i = 0; i < frames * 2; i++) pcm[i] = Clip16(accum[i]);
	EM_ASM({
		var fs = require('fs');
		if (Module.webcandcWav === undefined) {
			Module.webcandcWav = fs.openSync(process.env.WEBCANDC_WAV, 'w');
			Module.webcandcWavBytes = 0;
		}
		var fd = Module.webcandcWav;
		fs.writeSync(fd, HEAPU8.subarray($0, $0 + $1), 0, $1, 44 + Module.webcandcWavBytes);
		Module.webcandcWavBytes += $1;
		var n = Module.webcandcWavBytes;
		var h = Buffer.alloc(44);
		h.write('RIFF', 0); h.writeUInt32LE(36 + n, 4); h.write('WAVE', 8);
		h.write('fmt ', 12); h.writeUInt32LE(16, 16); h.writeUInt16LE(1, 20); h.writeUInt16LE(2, 22);
		h.writeUInt32LE($2, 24); h.writeUInt32LE($2 * 4, 28); h.writeUInt16LE(4, 32); h.writeUInt16LE(16, 34);
		h.write('data', 36); h.writeUInt32LE(n, 40);
		fs.writeSync(fd, h, 0, 44, 0);
	}, pcm.data(), frames * 4, MIX_RATE);
}
#else
static bool Wav_Enabled(void) { return false; }
static void Wav_Write(int const *, int) {}
#endif

/*
** Headless harness: with $WEBCANDC_DSOUND_TRACE set, log buffer creation and
** playback calls to stderr.
*/
#ifdef WEBCANDC_HEADLESS
static int TraceState = -1;
static bool Trace_On(void)
{
	if (TraceState < 0) {
		TraceState = EM_ASM_INT({ return (typeof process !== 'undefined' && process.env.WEBCANDC_DSOUND_TRACE) ? 1 : 0; });
	}
	return TraceState == 1;
}
#else
static bool Trace_On(void) { return false; }
#endif
#define TRACE(...) do { if (Trace_On()) { fprintf(stderr, "[dsound %8.0f] ", Now_Ms()); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)
static int NextBufferId = 1;

/*
** ---- IDirectSoundBuffer ------------------------------------------------------
*/
class WCSoundBuffer : public IDirectSoundBuffer
{
	public:
		ULONG Refs;
		int Id;					// For the trace.
		bool IsPrimary;
		DWORD Flags;
		WAVEFORMATEX Format;
		unsigned char *Data;
		DWORD Size;				// Bytes.
		DWORD Frames;			// Size / nBlockAlign.
		DWORD Frequency;		// Playback rate (Hz).
		LONG Volume;			// Hundredths of a dB, DSBVOLUME_MIN..0.
		LONG Pan;				// Hundredths of a dB, DSBPAN_LEFT..DSBPAN_RIGHT.
		float GainL, GainR;
		bool Playing;
		bool Looping;
		double Position;		// Play cursor, in sample frames.

		WCSoundBuffer(bool primary, DWORD flags, WAVEFORMATEX const *format, DWORD size) :
			Refs(1), Id(NextBufferId++), IsPrimary(primary), Flags(flags), Data(NULL), Size(size), Frames(0), Frequency(0),
			Volume(0), Pan(0), GainL(1.0f), GainR(1.0f), Playing(false), Looping(false), Position(0)
		{
			Set_Format(format);
			TRACE("buf %d create%s %lu Hz %d ch %d bit, %lu bytes", Id, primary ? " primary" : "",
				(unsigned long)format->nSamplesPerSec, format->nChannels, format->wBitsPerSample, (unsigned long)size);
			if (Size) {
				Data = new unsigned char[Size];
				memset(Data, Format.wBitsPerSample == 8 ? 0x80 : 0, Size);
			}
			Frames = Size / Format.nBlockAlign;
		}

		~WCSoundBuffer() { delete [] Data; }

		void Set_Format(WAVEFORMATEX const *format)
		{
			memset(&Format, 0, sizeof(Format));
			Format.wFormatTag = WAVE_FORMAT_PCM;
			Format.nChannels = format->nChannels;
			Format.nSamplesPerSec = format->nSamplesPerSec;
			Format.wBitsPerSample = format->wBitsPerSample;
			Format.nBlockAlign = (WORD)(Format.nChannels * Format.wBitsPerSample / 8);
			Format.nAvgBytesPerSec = Format.nSamplesPerSec * Format.nBlockAlign;
			Frequency = Format.nSamplesPerSec;
		}

		void Update_Gains(void)
		{
			float gain = (Volume <= DSBVOLUME_MIN) ? 0.0f : (float)pow(10.0, Volume / 2000.0);
			GainL = GainR = gain;
			if (Pan > 0) GainL *= (float)pow(10.0, -Pan / 2000.0);
			if (Pan < 0) GainR *= (float)pow(10.0, Pan / 2000.0);
		}

		int Sample(DWORD frame, int channel) const
		{
			unsigned char const *p = Data + frame * Format.nBlockAlign;
			if (Format.wBitsPerSample == 16) {
				p += channel * 2;
				return (Sint16)(p[0] | (p[1] << 8));
			}
			return ((int)p[channel] - 128) << 8;
		}

		void Advance(double frames)
		{
			Position += frames;
			if (Position >= Frames) {
				if (Looping) {
					Position = fmod(Position, (double)Frames);
				} else {
					Position = 0;
					Playing = false;
				}
			}
		}

		/*
		** Mix `count` output frames into accum (interleaved stereo), or with a
		** null accum just move the play cursor along.
		*/
		void Mix_Into(int *accum, int count)
		{
			double step = (double)Frequency / MIX_RATE;
			if (!accum) {
				Advance(step * count);
				return;
			}
			bool stereo = Format.nChannels == 2;
			for (int i = 0; i < count && Playing; i++) {
				DWORD f0 = (DWORD)Position;
				double t = Position - f0;
				DWORD f1 = f0 + 1;
				if (f1 >= Frames) f1 = Looping ? 0 : f0;
				int l0 = Sample(f0, 0);
				int l1 = Sample(f1, 0);
				int r0 = stereo ? Sample(f0, 1) : l0;
				int r1 = stereo ? Sample(f1, 1) : l1;
				double l = l0 + (l1 - l0) * t;
				double r = r0 + (r1 - r0) * t;
				accum[i * 2] += (int)(l * GainL);
				accum[i * 2 + 1] += (int)(r * GainR);
				Advance(step);
			}
		}

		DWORD Play_Cursor(void) const { return (DWORD)Position * Format.nBlockAlign; }

		STDMETHOD(QueryInterface)(REFIID, LPVOID *ppv) { *ppv = NULL; return E_NOINTERFACE; }
		STDMETHOD_(ULONG, AddRef)(void) { return ++Refs; }
		STDMETHOD_(ULONG, Release)(void);

		STDMETHOD(GetCaps)(LPDSBCAPS caps)
		{
			if (!caps) return DSERR_INVALIDPARAM;
			DWORD size = caps->dwSize;
			memset(caps, 0, sizeof(DSBCAPS));
			caps->dwSize = size;
			caps->dwFlags = Flags | DSBCAPS_LOCSOFTWARE;
			caps->dwBufferBytes = Size;
			return DS_OK;
		}

		STDMETHOD(GetCurrentPosition)(LPDWORD play, LPDWORD write);

		STDMETHOD(GetFormat)(LPWAVEFORMATEX format, DWORD size, LPDWORD written)
		{
			DWORD n = sizeof(WAVEFORMATEX);
			if (format) {
				if (size < n) n = size;
				memcpy(format, &Format, n);
			}
			if (written) *written = n;
			return DS_OK;
		}

		STDMETHOD(GetVolume)(LPLONG volume) { *volume = Volume; return DS_OK; }
		STDMETHOD(GetPan)(LPLONG pan) { *pan = Pan; return DS_OK; }
		STDMETHOD(GetFrequency)(LPDWORD freq) { *freq = Frequency; return DS_OK; }
		STDMETHOD(GetStatus)(LPDWORD status);
		STDMETHOD(Initialize)(LPDIRECTSOUND, LPDSBUFFERDESC) { return DSERR_ALREADYINITIALIZED; }

		STDMETHOD(Lock)(DWORD offset, DWORD bytes, LPVOID ptr1, LPDWORD len1, LPVOID ptr2, LPDWORD len2, DWORD flags)
		{
			if (IsPrimary) return DSERR_PRIOLEVELNEEDED;
			if (flags & DSBLOCK_FROMWRITECURSOR) GetCurrentPosition(NULL, &offset);
			if (flags & DSBLOCK_ENTIREBUFFER) bytes = Size;
			if (!ptr1 || !len1 || offset >= Size || bytes > Size) return DSERR_INVALIDPARAM;

			/*
			** A lock running past the end of the ring comes back in two parts.
			*/
			TRACE("buf %d lock %lu+%lu (play %lu)", Id, (unsigned long)offset, (unsigned long)bytes, (unsigned long)Play_Cursor());
			DWORD first = (offset + bytes > Size) ? Size - offset : bytes;
			*(LPVOID *)ptr1 = Data + offset;
			*len1 = first;
			if (ptr2) *(LPVOID *)ptr2 = (bytes > first) ? Data : NULL;
			if (len2) *len2 = bytes - first;
			if (bytes > first && !ptr2) return DSERR_INVALIDPARAM;
			return DS_OK;
		}

		STDMETHOD(Play)(DWORD, DWORD, DWORD flags);

		STDMETHOD(SetCurrentPosition)(DWORD pos);

		STDMETHOD(SetFormat)(LPWAVEFORMATEX format)
		{
			if (!IsPrimary) return DSERR_INVALIDCALL;
			if (!format || format->wFormatTag != WAVE_FORMAT_PCM) return DSERR_BADFORMAT;
			if (format->nChannels < 1 || format->nChannels > 2) return DSERR_BADFORMAT;
			if (format->wBitsPerSample != 8 && format->wBitsPerSample != 16) return DSERR_BADFORMAT;
			Set_Format(format);		// The mixer keeps its own output format regardless.
			return DS_OK;
		}

		STDMETHOD(SetVolume)(LONG volume)
		{
			if (volume < DSBVOLUME_MIN || volume > DSBVOLUME_MAX) return DSERR_INVALIDPARAM;
			if (volume != Volume) TRACE("buf %d volume %ld", Id, (long)volume);
			AudioLock lock;
			Volume = volume;
			Update_Gains();
			return DS_OK;
		}

		STDMETHOD(SetPan)(LONG pan)
		{
			if (pan < DSBPAN_LEFT || pan > DSBPAN_RIGHT) return DSERR_INVALIDPARAM;
			AudioLock lock;
			Pan = pan;
			Update_Gains();
			return DS_OK;
		}

		STDMETHOD(SetFrequency)(DWORD freq)
		{
			if (IsPrimary) return DSERR_CONTROLUNAVAIL;
			if (freq == DSBFREQUENCY_ORIGINAL) freq = Format.nSamplesPerSec;
			if (freq < DSBFREQUENCY_MIN || freq > DSBFREQUENCY_MAX) return DSERR_INVALIDPARAM;
			AudioLock lock;
			Frequency = freq;
			return DS_OK;
		}

		STDMETHOD(Stop)(void);
		STDMETHOD(Unlock)(LPVOID, DWORD, LPVOID, DWORD) { return DS_OK; }
		STDMETHOD(Restore)(void) { return DS_OK; }
};

/*
** ---- Mixer -------------------------------------------------------------------
*/

/*
** Mix `count` frames of every playing buffer into out (or, with out == NULL,
** into the null sink), advancing their play cursors.
*/
static void Mix(Sint16 *out, int count)
{
	bool capture = Wav_Enabled();
	bool render = out || capture;
	if (render) Accum.assign(count * 2, 0);
	for (size_t i = 0; i < Secondaries.size(); i++) {
		if (Secondaries[i]->Playing) Secondaries[i]->Mix_Into(render ? Accum.data() : NULL, count);
	}

	/*
	** A stopped primary buffer means silence (the game stops it on focus loss).
	*/
	bool audible = !Primary || Primary->Playing;
	if (out) {
		for (int i = 0; i < count * 2; i++) out[i] = audible ? Clip16(Accum[i]) : 0;
	}
	if (capture) {
		if (!audible) memset(Accum.data(), 0, count * 2 * sizeof(int));
		Wav_Write(Accum.data(), count);
	}
}

static void SDLCALL Device_Callback(void *, Uint8 *stream, int len)
{
	double now = Now_Ms();
	Mix((Sint16 *)stream, len / 4);
	DeviceMixMs = now;
	MixedToMs = now;
}

/*
** If the device isn't consuming audio (none at all, or stalled), advance the
** play cursors to the present by the wall clock. Called whenever the game is
** about to look at, or change, playback state.
*/
static void Catch_Up(void)
{
	double now = Now_Ms();
	if (Device && now - DeviceMixMs < STALL_MS) return;
	if (MixedToMs < now - MAX_CATCHUP_MS) MixedToMs = now - MAX_CATCHUP_MS;
	int frames = (int)((now - MixedToMs) * MIX_RATE / 1000.0);
	if (frames <= 0) return;
	Mix(NULL, frames);
	MixedToMs += frames * 1000.0 / MIX_RATE;
}

ULONG WCSoundBuffer::Release(void)
{
	ULONG r = --Refs;
	if (!r) {
		AudioLock lock;
		for (size_t i = 0; i < Secondaries.size(); i++) {
			if (Secondaries[i] == this) {
				Secondaries.erase(Secondaries.begin() + i);
				break;
			}
		}
		if (Primary == this) Primary = NULL;
		delete this;
	}
	return r;
}

HRESULT WCSoundBuffer::GetCurrentPosition(LPDWORD play, LPDWORD write)
{
	AudioLock lock;
	Catch_Up();
	DWORD p = IsPrimary ? 0 : Play_Cursor();
	if (play) *play = p;
	if (write) {
		/*
		** The write cursor: where it's safe to write, about one mixer
		** callback's worth ahead of the play cursor.
		*/
		DWORD ahead = (DWORD)((double)MIX_FRAMES * Frequency / MIX_RATE) * Format.nBlockAlign;
		*write = Size ? (p + ahead) % Size : 0;
	}
	return DS_OK;
}

HRESULT WCSoundBuffer::GetStatus(LPDWORD status)
{
	if (!status) return DSERR_INVALIDPARAM;
	AudioLock lock;
	Catch_Up();
	*status = Playing ? (DSBSTATUS_PLAYING | (Looping ? DSBSTATUS_LOOPING : 0)) : 0;
	return DS_OK;
}

HRESULT WCSoundBuffer::Play(DWORD, DWORD, DWORD flags)
{
	AudioLock lock;
	Catch_Up();		// Time already past isn't this buffer's.
	if (!Playing && Trace_On()) {
		char hex[65] = "";
		for (DWORD i = 0; i < 32 && i < Size; i++) sprintf(hex + i * 2, "%02x", Data[i]);
		TRACE("buf %d play%s at %lu, data %s", Id, (flags & DSBPLAY_LOOPING) ? " looping" : "", (unsigned long)Play_Cursor(), hex);
	}
	Playing = true;
	Looping = (flags & DSBPLAY_LOOPING) != 0;
	return DS_OK;
}

HRESULT WCSoundBuffer::SetCurrentPosition(DWORD pos)
{
	if (IsPrimary) return DSERR_INVALIDCALL;
	AudioLock lock;
	Catch_Up();
	Position = Frames ? (double)((pos % Size) / Format.nBlockAlign) : 0;
	TRACE("buf %d position %lu", Id, (unsigned long)pos);
	return DS_OK;
}

HRESULT WCSoundBuffer::Stop(void)
{
	AudioLock lock;
	Catch_Up();
	if (Playing) TRACE("buf %d stop at %lu", Id, (unsigned long)Play_Cursor());
	Playing = false;
	return DS_OK;
}

/*
** ---- IDirectSound ------------------------------------------------------------
*/
class WCDirectSound : public IDirectSound
{
	public:
		ULONG Refs;
		WCDirectSound() : Refs(1) {}

		STDMETHOD(QueryInterface)(REFIID, LPVOID *ppv) { *ppv = NULL; return E_NOINTERFACE; }
		STDMETHOD_(ULONG, AddRef)(void) { return ++Refs; }

		STDMETHOD_(ULONG, Release)(void)
		{
			ULONG r = --Refs;
			if (!r) {
				if (Device) {
					SDL_CloseAudioDevice(Device);
					Device = 0;
				}
				delete this;
			}
			return r;
		}

		STDMETHOD(CreateSoundBuffer)(LPDSBUFFERDESC desc, LPLPDIRECTSOUNDBUFFER out, IUnknown *)
		{
			if (!out) return DSERR_INVALIDPARAM;
			*out = NULL;
			if (!desc) return DSERR_INVALIDPARAM;

			if (desc->dwFlags & DSBCAPS_PRIMARYBUFFER) {
				/*
				** There is one primary buffer; asking again returns it.
				*/
				if (Primary) {
					Primary->AddRef();
				} else {
					WAVEFORMATEX format;
					memset(&format, 0, sizeof(format));
					format.nChannels = 2;
					format.nSamplesPerSec = MIX_RATE;
					format.wBitsPerSample = 16;
					Primary = new WCSoundBuffer(true, desc->dwFlags, &format, 0);
				}
				*out = Primary;
				return DS_OK;
			}

			WAVEFORMATEX const *format = desc->lpwfxFormat;
			if (!format || format->wFormatTag != WAVE_FORMAT_PCM) return DSERR_BADFORMAT;
			if (format->nChannels < 1 || format->nChannels > 2) return DSERR_BADFORMAT;
			if (format->wBitsPerSample != 8 && format->wBitsPerSample != 16) return DSERR_BADFORMAT;
			if (format->nSamplesPerSec < DSBFREQUENCY_MIN || format->nSamplesPerSec > DSBFREQUENCY_MAX) return DSERR_BADFORMAT;
			if (desc->dwBufferBytes < DSBSIZE_MIN || desc->dwBufferBytes > DSBSIZE_MAX) return DSERR_INVALIDPARAM;

			WCSoundBuffer *buffer = new WCSoundBuffer(false, desc->dwFlags, format, desc->dwBufferBytes);
			AudioLock lock;
			Secondaries.push_back(buffer);
			*out = buffer;
			return DS_OK;
		}

		STDMETHOD(GetCaps)(LPDSCAPS caps)
		{
			if (!caps) return DSERR_INVALIDPARAM;
			DWORD size = caps->dwSize;
			memset(caps, 0, sizeof(DSCAPS));
			caps->dwSize = size;
			caps->dwFlags = DSCAPS_PRIMARYMONO | DSCAPS_PRIMARYSTEREO | DSCAPS_PRIMARY8BIT | DSCAPS_PRIMARY16BIT |
				DSCAPS_CONTINUOUSRATE | DSCAPS_EMULDRIVER | DSCAPS_SECONDARYMONO | DSCAPS_SECONDARYSTEREO |
				DSCAPS_SECONDARY8BIT | DSCAPS_SECONDARY16BIT;
			caps->dwMinSecondarySampleRate = DSBFREQUENCY_MIN;
			caps->dwMaxSecondarySampleRate = DSBFREQUENCY_MAX;
			return DS_OK;
		}

		STDMETHOD(DuplicateSoundBuffer)(LPDIRECTSOUNDBUFFER, LPLPDIRECTSOUNDBUFFER out)
		{
			if (out) *out = NULL;
			return DSERR_UNSUPPORTED;	// Unused by the game.
		}

		STDMETHOD(SetCooperativeLevel)(HWND, DWORD) { return DS_OK; }
		STDMETHOD(Compact)(void) { return DS_OK; }
		STDMETHOD(GetSpeakerConfig)(LPDWORD config) { *config = DSSPEAKER_STEREO; return DS_OK; }
		STDMETHOD(SetSpeakerConfig)(DWORD) { return DS_OK; }
		STDMETHOD(Initialize)(GUID *) { return DSERR_ALREADYINITIALIZED; }
};

extern "C" HRESULT WINAPI DirectSoundCreate(GUID *, LPDIRECTSOUND *out, IUnknown *)
{
	if (!out) return DSERR_INVALIDPARAM;
	*out = NULL;
#ifndef WEBCANDC_HEADLESS
	if (!Device) {
		if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
			fprintf(stderr, "[webcandc] SDL audio init failed: %s\n", SDL_GetError());
			return DSERR_NODRIVER;
		}
		SDL_AudioSpec want, have;
		SDL_zero(want);
		want.freq = MIX_RATE;
		want.format = AUDIO_S16SYS;
		want.channels = 2;
		want.samples = MIX_FRAMES;
		want.callback = Device_Callback;
		Device = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);	// SDL converts to the device's format.
		if (!Device) {
			fprintf(stderr, "[webcandc] SDL_OpenAudioDevice failed: %s\n", SDL_GetError());
			return DSERR_NODRIVER;
		}
		SDL_PauseAudioDevice(Device, 0);
	}
	DeviceMixMs = Now_Ms();
#endif
	MixedToMs = Now_Ms();
	*out = new WCDirectSound();
	return DS_OK;
}
