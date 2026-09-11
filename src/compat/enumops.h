/*
** webcandc: bodies for the enum operators the game declares (DEFINES.H,
** GADGET.H, EDIT.H, DRIVE.H).
**
** Under the pre-standard rules Watcom followed, a declaration such as
**     inline BulletType operator++(BulletType &, int);
** was satisfied by the matching operator templates in JSHELL.H. Standard C++
** treats it as a separate function that nobody defines, so these give each
** one exactly the JSHELL.H template body. Included at the end of FUNCTION.H.
*/
#ifndef WEBCANDC_ENUMOPS_H
#define WEBCANDC_ENUMOPS_H

#define WEBCANDC_ENUM_POSTINC(T) \
	inline T operator++(T & a, int) { T aa = a; a = (T)((int)a + (int)1); return aa; }

#define WEBCANDC_ENUM_BITOPS(T) \
	inline T operator|(T t1, T t2) { return (T)((int)t1 | (int)t2); } \
	inline T operator&(T t1, T t2) { return (T)((int)t1 & (int)t2); } \
	inline T operator~(T t1) { return (T)(~(int)t1); }

WEBCANDC_ENUM_POSTINC(ThemeType)
WEBCANDC_ENUM_POSTINC(HousesType)
WEBCANDC_ENUM_POSTINC(ScenarioPlayerType)
WEBCANDC_ENUM_POSTINC(ScenarioDirType)
WEBCANDC_ENUM_POSTINC(ScenarioVarType)
WEBCANDC_ENUM_POSTINC(LayerType)
WEBCANDC_ENUM_POSTINC(BulletType)
WEBCANDC_ENUM_POSTINC(StructType)
WEBCANDC_ENUM_POSTINC(OverlayType)
WEBCANDC_ENUM_POSTINC(InfantryType)
WEBCANDC_ENUM_POSTINC(UnitType)
WEBCANDC_ENUM_POSTINC(AircraftType)
WEBCANDC_ENUM_POSTINC(TemplateType)
WEBCANDC_ENUM_POSTINC(TerrainType)
WEBCANDC_ENUM_POSTINC(SmudgeType)
WEBCANDC_ENUM_POSTINC(AnimType)
WEBCANDC_ENUM_POSTINC(DoType)
WEBCANDC_ENUM_POSTINC(TheaterType)
WEBCANDC_ENUM_POSTINC(BSizeType)
WEBCANDC_ENUM_POSTINC(FacingType)

WEBCANDC_ENUM_BITOPS(ThreatType)
WEBCANDC_ENUM_BITOPS(TextPrintType)
WEBCANDC_ENUM_BITOPS(EditClass::EditStyle)
WEBCANDC_ENUM_BITOPS(DriveClass::TrackControlType)
WEBCANDC_ENUM_BITOPS(GadgetClass::FlagEnum)

#endif
