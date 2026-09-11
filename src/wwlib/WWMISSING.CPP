/*
** webcandc: small engine functions that are declared in the wwlib headers but
** whose bodies were not part of the Remastered WIN32LIB drop:
**
**   DELAY.CPP     -- Delay
**   LIB.CPP       -- Convert_RGB_To_HSV, Convert_HSV_To_RGB
**   ALLOC.CPP     -- Largest_Mem_Block (Watcom/DPMI query; stubbed)
**   PROFILE       -- Profile_Init, Profile_End, Start_Profiler, Stop_Profiler
**                    (sampling profiler; no-ops)
**   PLAYCD.CPP    -- GetCDClass constructor/destructor (no CD drives)
*/

#include "WWSTD.H"
#include "TIMER.H"
#include "MISC.H"
#include "PROFILE.H"
#include "PLAYCD.H"


/***************************************************************************
 * Delay -- Waits for the given number of 60Hz system ticks.               *
 *                                                                         *
 * Busy-waits on the system tick count (TickCount's time base); the timer  *
 * getter is the yield point for the browser build.                        *
 *=========================================================================*/
void Delay(int duration)
{
	TimerClass timer(BT_SYSTEM, TRUE);

	while (duration-- > 0) {
		long const count = timer.Time() + 1L;
		while (count >= timer.Time()) {
			;
		}
	}
}


/***************************************************************************
 * HSV <-> RGB conversion (from WIN32LIB MISC/LIB.CPP).                    *
 *                                                                         *
 * RGB components are VGA 6-bit (0..63); H, S and V are 0..255.            *
 *=========================================================================*/
#define HSV_BASE 255		// This is used to get a little better precision on HSV conversion.
#define RGB_BASE 63		// Not 64, this is really the max value.

static unsigned Divide_With_Round(unsigned num, unsigned den)
{
	// return num/den + (0 or 1).  1 if the remainder is more than half the denominator.
	return (num / den) + (unsigned)((num % den) >= ((den + 1) >> 1));
}


void Convert_RGB_To_HSV(unsigned int r, unsigned int g, unsigned int b, unsigned int *h, unsigned int *s, unsigned int *v)
{
	unsigned int m, r1, g1, b1, tmp;

	// Convert RGB base to HSV base.
	r = Divide_With_Round((r * HSV_BASE), RGB_BASE);
	g = Divide_With_Round((g * HSV_BASE), RGB_BASE);
	b = Divide_With_Round((b * HSV_BASE), RGB_BASE);

	// Set hue to default.
	*h = 0;

	// Set v = Max(r,g,b) to find dominant primary color.
	*v = (r > g) ? r : g;
	if (b > *v) *v = b;

	// Set m = min(r,g,b) to find amount of white.
	m = (r < g) ? r : g;
	if (b < m) m = b;

	// Determine the normalized saturation.
	if (*v != 0) {
		*s = Divide_With_Round((*v - m) * HSV_BASE, *v);
	} else {
		*s = 0;
	}

	if (*s != 0) {
		tmp = *v - m;
		r1 = Divide_With_Round((*v - r) * HSV_BASE, tmp);
		g1 = Divide_With_Round((*v - g) * HSV_BASE, tmp);
		b1 = Divide_With_Round((*v - b) * HSV_BASE, tmp);

		// Find effect of second most predominant color.
		// In which section of the hexagon of colors does the color lie?
		if ((*v) == r) {
			if (m == g) {
				*h = 5 * HSV_BASE + b1;
			} else {
				*h = 1 * HSV_BASE - g1;
			}
		} else {
			if ((*v) == g) {
				if (m == b) {
					*h = 1 * HSV_BASE + r1;
				} else {
					*h = 3 * HSV_BASE - b1;
				}
			} else {
				// *v == b
				if (m == r) {
					*h = 3 * HSV_BASE + g1;
				} else {
					*h = 5 * HSV_BASE - r1;
				}
			}
		}

		// Divide by six and round.
		*h = Divide_With_Round(*h, 6);
	}
}


void Convert_HSV_To_RGB(unsigned int h, unsigned int s, unsigned int v, unsigned int *r, unsigned int *g, unsigned int *b)
{
	unsigned int i;				// Integer part.
	unsigned int f;				// Fractional or remainder part.  f/HSV_BASE gives fraction.
	unsigned int tmp;				// Temporary variable to help with calculations.
	unsigned int values[7];		// Possible rgb values.  Don't use zero.

	h *= 6;
	f = h % HSV_BASE;

	// Set up possible red, green and blue values.
	values[0] = 0;
	values[1] =
	values[2] = v;

	tmp = Divide_With_Round(s * f, HSV_BASE);
	values[3] = Divide_With_Round(v * (HSV_BASE - tmp), HSV_BASE);

	values[4] =
	values[5] = Divide_With_Round(v * (HSV_BASE - s), HSV_BASE);

	tmp = HSV_BASE - Divide_With_Round(s * (HSV_BASE - f), HSV_BASE);
	values[6] = Divide_With_Round(v * tmp, HSV_BASE);

	// This should not be rounded.
	i = h / HSV_BASE;

	i += (i > 4) ? -4 : 2;
	*r = Divide_With_Round(values[i] * RGB_BASE, HSV_BASE);

	i += (i > 4) ? -4 : 2;
	*b = Divide_With_Round(values[i] * RGB_BASE, HSV_BASE);

	i += (i > 4) ? -4 : 2;
	*g = Divide_With_Round(values[i] * RGB_BASE, HSV_BASE);
}


/***************************************************************************
 * Largest_Mem_Block -- Size of the largest free memory block.             *
 *                                                                         *
 * The DOS/DPMI query has no equivalent; report a large but not absurd     *
 * amount (64MB) so free-memory checks pass without inviting callers to    *
 * allocate the whole address space.                                       *
 *=========================================================================*/
extern "C" unsigned long Largest_Mem_Block(void)
{
	return 64UL * 1024UL * 1024UL;
}


/***************************************************************************
 * Sampling profiler (PROFILE.H) -- not supported; no-ops.                 *
 *=========================================================================*/
extern "C" void __cdecl Profile_Init(void) {}
extern "C" void __cdecl Profile_End(void) {}
extern "C" void __cdecl Start_Profiler(void) {}
extern "C" void __cdecl Stop_Profiler(void) {}


/***************************************************************************
 * GetCDClass -- enumerates CD-ROM drives.                                 *
 *                                                                         *
 * There are no CD drives in the browser: CDCount is 0, so                 *
 * Get_First_CD_Drive()/Get_Next_CD_Drive() return -1 (NO_CD_DRIVE) and    *
 * Get_Number_Of_Drives() returns 0. Game data must be reachable through   *
 * CCFileClass search drives (otherwise INIT.CPP reports TXT_CD_ERROR1).   *
 *=========================================================================*/
GetCDClass::GetCDClass(VOID)
{
	for (int i = 0; i < MAX_CD_DRIVES; i++) {
		CDDrives[i] = NO_CD_DRIVE;
	}
	CDCount = 0;
	CDIndex = 0;
}


GetCDClass::~GetCDClass(VOID)
{
}
