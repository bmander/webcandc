/*
** webcandc: text strings the game's code (v1.07) knows but the freeware
** disc's CONQUER.ENG (742 strings) predates -- the map-screen captions and
** the Bonus Missions menu. Extract_String does no bounds check, so asking
** for them read past the table (the main menu showed garbage). The text is
** the English given alongside each TXT_ constant in CONQUER.H.
*/
#include "../game/FUNCTION.H"

static struct { int Number; char const *Text; } const Missing[] = {
	{ TXT_READING_IMAGE_DATA, "READING IMAGE DATA" },
	{ TXT_ANALYZING, "ANALYZING" },
	{ TXT_ENHANCING_IMAGE_DATA, "ENHANCING IMAGE DATA" },
	{ TXT_ISOLATING_OPERATIONAL_THEATER, "ISOLATING OPERATIONAL THEATER" },
	{ TXT_ESTABLISHING_TRADITIONAL_BOUNDARIES, "ESTABLISHING TRADITIONAL BOUNDARIES" },
	{ TXT_FOR_VISUAL_REFERENCE, "FOR VISUAL REFERENCE" },
	{ TXT_ENHANCING_IMAGE, "ENHANCING IMAGE" },
	{ TXT_BONUS_MISSIONS, "Bonus Missions" },
	{ TXT_BONUS_MISSION_1, "Bonus Mission 1" },
	{ TXT_BONUS_MISSION_2, "Bonus Mission 2" },
	{ TXT_BONUS_MISSION_3, "Bonus Mission 3" },
	{ TXT_BONUS_MISSION_4, "Bonus Mission 4" },
	{ TXT_BONUS_MISSION_5, "Bonus Mission 5" },
};

char const * WebCandC_Text_String(int string)
{
	/*
	** The string table starts with its offset list, so the first offset,
	** halved, is the number of strings it holds.
	*/
	if (SystemStrings && string >= 0 && string < 4567) {
		unsigned count = ((unsigned short const *)SystemStrings)[0] / 2;
		if ((unsigned)string >= count) {
			for (unsigned i = 0; i < sizeof(Missing) / sizeof(Missing[0]); i++) {
				if (Missing[i].Number == string) return Missing[i].Text;
			}
			return "";
		}
	}
	return Extract_String(SystemStrings, string);
}
