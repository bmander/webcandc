/*
** webcandc: TEMPORARY silent audio. The game's sound API (wwlib/AUDIO.H) was
** implemented by the Westwood SOUNDIO library on DirectSound, which isn't in
** the Remastered engine sources. Audio_Init reports no sound card, which the
** game handles by running silently. Replaced by the real SOUNDIO port.
*/
#include <windows.h>
#include <dsound.h>
#include "audio.h"

LPDIRECTSOUND SoundObject = NULL;
LPDIRECTSOUNDBUFFER PrimaryBufferPtr = NULL;
SFX_Type SoundType = SFX_NONE;
Sample_Type SampleType = SAMPLE_NONE;
int StreamLowImpact = 0;
void (*Audio_Focus_Loss_Function)(void) = NULL;

BOOL Audio_Init(HWND, int, BOOL, int, int) { return FALSE; }
void Sound_End(void) {}
void __cdecl Sound_Callback(void) {}
void *Load_Sample(char const *) { return NULL; }
void Free_Sample(void const *) {}
int Play_Sample(void const *, int, int, signed short) { return -1; }
void Stop_Sample(int) {}
BOOL Sample_Status(int) { return FALSE; }
BOOL Is_Sample_Playing(void const *) { return FALSE; }
void Stop_Sample_Playing(void const *) {}
int File_Stream_Sample_Vol(char const *, int, BOOL) { return -1; }
int Set_Score_Vol(int volume) { return volume; }
void Fade_Sample(int, int) {}
int Get_Digi_Handle(void) { return -1; }
BOOL Set_Primary_Buffer_Format(void) { return FALSE; }
BOOL Start_Primary_Sound_Buffer(BOOL) { return FALSE; }
void Stop_Primary_Sound_Buffer(void) {}
