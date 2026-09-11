/*
** webcandc: TEMPORARY no-movie VQA player. VQA_Open fails, which the game
** already treats as "movie not available" and skips it. Replaced by the
** WINVQ player port (src/vqa).
*/
#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include <vqa32/vqaplay.h>

void VQA_DefaultConfig(VQAConfig *config) { memset(config, 0, sizeof(*config)); }
VQAHandle *VQA_Alloc(void) { return (VQAHandle *)calloc(1, sizeof(VQAHandle)); }
void VQA_Free(VQAHandle *vqa) { free(vqa); }
void VQA_Init(VQAHandle *, long (*)(VQAHandle *, long, void *, long)) {}
long VQA_Open(VQAHandle *, char const *, VQAConfig *) { return -1; }
void VQA_Close(VQAHandle *) {}
long VQA_Play(VQAHandle *, long) { return 0; }
void VQA_PauseAudio(void) {}
void VQA_ResumeAudio(void) {}
