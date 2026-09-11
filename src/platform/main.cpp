/*
** webcandc: program entry. Brings up SDL, then hands control to the 1995
** WinMain (STARTUP.CPP) exactly as Windows 95 would have. The game data lives
** in /data in the Emscripten file system; GetModuleFileName reports the
** executable there, so the game's own "chdir to the .EXE directory" lands in it.
*/
#include <windows.h>
#include <stdio.h>
#include <string.h>

int PASCAL WinMain(HINSTANCE instance, HINSTANCE, char *command_line, int command_show);

int main(int argc, char **argv)
{
	static char command_line[512];
	command_line[0] = '\0';
	for (int i = 1; i < argc; i++) {
		if (i > 1) strncat(command_line, " ", sizeof(command_line) - strlen(command_line) - 1);
		strncat(command_line, argv[i], sizeof(command_line) - strlen(command_line) - 1);
	}

	/*
	** No CD-ROM: point the game's own -CD<path> option (INIT.CPP) at the
	** directory holding the data files, as the hard-disk install did.
	*/
	if (!command_line[0]) strcpy(command_line, "-CD.");

	WebCandC_Init();
	fprintf(stderr, "[webcandc] entering WinMain(\"%s\")\n", command_line);
	int result = WinMain((HINSTANCE)1, NULL, command_line, SW_SHOWNORMAL);
	fprintf(stderr, "[webcandc] WinMain returned %d\n", result);
	return result;
}
