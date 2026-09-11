# Platform surface survey (Win32 / DirectX / asm) — Phase 1 reference

Surveyed from `src/game` (EA TD 1995 source) and `src/wwlib` (Remastered TIBERIANDAWN/WIN32LIB).
RM = reference/CnC_Remastered_Collection/TIBERIANDAWN, VC = reference/Vanilla-Conquer, RA = reference/CnC_Red_Alert/WIN32LIB.

## Remaster stubs that must be restored (src/wwlib is NOT the 1995 lib)
- **Video**: `Set_Video_Mode` = `return TRUE;` with real body under `#if (0)` (DDRAW.CPP:451-455). `DirectDrawObject` stays NULL but `GraphicBufferClass::DD_Init` calls `DirectDrawObject->CreateSurface` (GBUFFER.CPP:267).
- **Input**: `WWKeyboardClass::Message_Handler` is `return; #if (0)` (KEYBOARD.CPP:313-316); original body at 316-391.
- **Mouse**: 60 Hz `timeSetEvent(...Process_Mouse...)` commented out (MOUSEWW.CPP:93-94); nothing calls `Process_Mouse`.
- **Asm**: `DrawMisc.cpp` (21 `__asm`), `IRANDOM.CPP` (2), RM `MiscAsm.cpp` (20) are MSVC inline asm.
- `game/STARTUP.CPP` contains Latin-1 bytes — use `grep -a` / `LC_ALL=C`.

## 1. Win32 API (critical path vs peripheral)
Critical: window creation (WINSTUB.CPP:398-435), message pump (Peek/Get/Translate/DispatchMessage — WINSTUB.CPP:190-211,460; KEYBOARD.CPP:402-407), `GetCursorPos` (MOUSEWW ×5, raw screen coords), `GetKeyState`/`GetAsyncKeyState`/`VkKeyScan` (KEYBOARD.CPP:244-248,293,71), `timeSetEvent`/`timeBeginPeriod` (TIMERINI.CPP:108-120 — the only tick source), critical sections (MOUSEWW Block_Mouse, called from every GraphicBufferClass::Lock), `Sleep` (STARTUP timer self-test: `Sleep(1000)` then fails if ticks didn't advance), DirectDraw.
Peripheral (stub): DDE (DDE.CPP), registry (INTERNET.CPP), WChat/Winsock (TCPIP.CPP), modem (NULLDLG), DialogBox, MessageBox (95 in DDRAW error text), CreateThread (CONQUER threaded reader; CONQUER.CPP:2053 busy-waits `while (ThreadReading || timer.Time()){}`), CreateFile/GetVolumeInformation (CONQUER Get_CD_Index), GlobalMemoryStatus, IsBadReadPtr, LoadLibrary, _dos_find* (CDFILE, LOADDLG), _dos_getdrive/setdrive/getdiskfree.

## 2. DirectDraw
Methods used: DirectDrawCreate, SetCooperativeLevel, SetDisplayMode, CreatePalette (DDRAW.CPP:462-493, currently dead), CreateSurface (GBUFFER.CPP:267), GetCaps (DDRAW.CPP:594,638; STATS.CPP:399), WaitForVerticalBlank (DDRAW.CPP:692), RestoreDisplayMode/Release; Surface: Lock/Unlock (GBUFFER.CPP:569/632), Blt (GBUFFER.CPP ~700 DD_Linear_Blit_To_Linear with DDBLT_KEYSRC; GBUFFER.H:1206 Fill_Rect color fill ≥32×32 when AllowHardwareBlitFills), GetBltStatus (GBUFFER.H:1173; DDRAW.CPP:782), SetPalette (DDRAW.CPP:747), GetCaps (STARTUP checks DDSCAPS_SYSTEMMEMORY), Release, restore via SurfaceMonitorClass (DDRAW.CPP:804+), AddAttachedSurface (Attach_DD_Surface). Palette: SetEntries(0,0,256) (DDRAW.CPP:752).
Surfaces (STARTUP WinMain): ScreenWidth=640, ScreenHeight=400 (GLOBALS.CPP:1023) 8bpp, fallback 640×480. VisiblePage = primary (GBC_VISIBLE|GBC_VIDEOMEM) and PaletteSurface. HiddenPage = offscreen video surface or malloc buffer. SeenBuff/HidPage 640×400 viewports (y=40 if 480 mode). No flip chain.
Lock: GraphicBufferClass::Lock (GBUFFER.CPP:528) ref-counted; returns FALSE if !GameInFocus; takes Block_Mouse; sets Offset=lpSurface, Pitch = lPitch − Width (extra bytes per row).
Palette: Set_Palette (PALETTE.CPP:86) → Set_DD_Palette (DDRAW.CPP:719) converts 6-bit 768-byte palette (<<2) → SetEntries. Fade_Palette_To (PALETTE.CPP:157) steps on TickCount.
Present: game draws to HidPage then `HidPage.Blit(SeenBuff)` (~30 sites). Blit inline (GBUFFER.H:765) → DD_Linear_Blit_To_Linear when both are DD surfaces else software Linear_Blit_To_Linear. Some UI draws straight to SeenBuff (primary). Present when the primary is unlocked/blitted-to or at yield points.

## 3. Audio
Game expects WWLIB SOUNDIO API (wwlib/AUDIO.H:119-149); **none implemented in src/wwlib** (no SOUNDIO/SOUNDINT/SOUNDLCK/AUDUNCMP/SOSCODEC).
Used: Play_Sample (44), Is_Sample_Playing, Stop_Sample, Sample_Status, Fade_Sample, File_Stream_Sample_Vol (THEME.CPP:321,329), Sound_Callback, Load_Sample/Free_Sample (ENDING), Audio_Init(MainWindow,16,false,22050,0) (STARTUP), Sound_End, Set_Score_Vol, Get_Digi_Handle, Set_Primary_Buffer_Format, Start_/Stop_Primary_Sound_Buffer (WINSTUB), Stop_Sample_Playing, Suspend_/Resume_Audio_Thread (CONQUER.CPP:2305), Audio_Focus_Loss_Function.
Conflict: wwlib/FUNCTION.H:34 declares stale DOS `Audio_Init(int,int,int,int)`.
Refs: RA AUDIO/SOUNDIO.CPP (DirectSound, all functions), SOUNDINT.CPP, SOUNDLCK.CPP, AUDUNCMP.ASM, SOSCODEC.ASM; VC common/soundio.cpp (+soundio_imp.h backends openal/null), auduncmp.cpp (Audio_Unzap), soscodec.cpp.

## 4. Input
Windows_Procedure (WINSTUB.CPP:231) forwards mouse/key messages to `Kbd.Message_Handler` (line 262); handles WM_DESTROY, WM_ACTIVATEAPP (GameInFocus, Focus_Loss, Restore_Surfaces), WM_SYSCOMMAND, CC_GOT_FOCUS.
WWKeyboardClass (KEYBOARD.CPP; global Kbd at GLOBALS.CPP:1022): Message_Handler → Put_Key_Message(VK) (reads modifiers via GetKeyState); mouse buttons → Put_Key_Message(VK_xBUTTON[,release,dbl]) then Put(x), Put(y). Keycodes = Windows VK + WWKEY_* bits; ASCII tables from VkKeyScan (59-90). Down() uses GetAsyncKeyState. VC common/sdl_keymap.h + wwkeyboard_sdl2.cpp map SDL↔VK.
WWMouseClass (MOUSEWW.CPP; `new WWMouseClass(&SeenBuff,32,32)` in STARTUP): position from GetCursorPos (raw coords); software cursor via ::Draw_Mouse (missing); Process_Mouse (185) per-tick visible-page cursor update now has no caller.

## 5. Timers
STARTUP: `WindowsTimer = new WinTimerClass(60,FALSE)` → timeSetEvent(1000/60, TIME_PERIODIC) → Timer_Callback → Update_Tick_Count (SysTicks++, UserTicks++). TimerClass::Time (TIMER.CPP:94) reads Get_System_Tick_Count()/Get_User_Tick_Count(). TickCount = TimerClass(BT_SYSTEM) (TIMERINI.CPP:67). Shim: make tick getters pull-based from SDL_GetTicks()*60/1000.

## 6. Main loop, pumps, blocking waits (Asyncify yield points)
WinMain (STARTUP.CPP:120) → Create_Main_Window (WINSTUB.CPP:388) → Audio_Init → Set_Video_Mode → page init → Main_Game (CONQUER.CPP:138) → `while (Select_Game()) { ... Main_Loop() }`.
Main_Loop (CONQUER.CPP:1458): focus check, Map.Input, Map.Render, Logic.AI, Queue_AI, Call_Back, Frame++, Sync_Delay (1400) which loops `while (FrameTimer.Time()) {Color_Cycle; Call_Back; Map.Input; Map.Render;}`.
Message pumps: Message_Loop() (KEYBOARD.CPP:396); WWKeyboardClass::Check() (159) calls it (so every Keyboard::Check/Get/Map.Input pumps); WWKeyboardClass::Get() (180) blocks; Check_For_Focus_Loss (WINSTUB.CPP:183); Window_Dialog_Box (WINSTUB.CPP:443).
Call_Back() (CONQUER.CPP:1117) does NOT pump (Theme.AI, Speak_AI, IPX). Timer-only spin loops: MSGBOX.CPP:411, ENDING.CPP:76/91/235/250, PALETTE.CPP:197 (Fade_Palette_To), MAPSEL.CPP:372, SCENARIO.CPP:352/572, SCORE.CPP:1857, CONQUER.CPP:2053, INTERNET/STATS/NULLMGR.
**Yield in Message_Loop, Call_Back, and the tick-count getter** (throttled) — every busy-wait reads the timer.

## 7. Missing engine functions (no C/C++ body)
A. Watcom `#pragma aux` (header-only): Coord_Cell (FUNCTION.H:202), Distance_Coord (FUNCTION.H:827), Set_Bit/Get_Bit/First_True_Bit/First_False_Bit/Bound (JSHELL.H:124-189), Fixed_To_Cardinal/Cardinal_To_Fixed (JSHELL.H:206/219), calcx/calcy (COORD.CPP:405/417), dss_* (DPMI.CPP), output (MONOC.CPP). Impl: RM MiscAsm.cpp (:875, :54, :784-848, :661/725), VC tiberiandawn/coord.cpp, common/misc.cpp.
B. Only in src .ASM: Buffer_Frame_To_Page (game/KEYFBUFF.ASM → VC common/keybuff.cpp); strtrim, Fat_Put_Pixel, Conquer_Build_Fading_Table, Remove_From_List, Get_EAX (SUPPORT.ASM → RM MiscAsm.cpp:1331/1410/1078, VC misc.cpp/drawmisc.cpp/fading.cpp); Buffer_Print, Get_Font_Palette_Ptr (TXTPRNT.ASM → VC common/font.cpp); Asm_Interpolate* + Asm_Create_Palette_Interpolation_Table (WINASM.ASM → VC common/winasm.cpp); ModeX_Blit, Set_Palette_Register, Stop_Execution, Greenleaf (WINASM.ASM → stub); Detect_MMX_Availability, Init_MMX (MMX.ASM → stub 0); IPX asm → stub; Install_Page_Fault_Handle → stub; Buffer_To_Buffer (wwlib/TOBUFF.ASM → VC common/tobuff.cpp); Desired_Facing256 (FACINGFF.ASM → RM MiscAsm.cpp:204, VC face.cpp); Buffer_Remap, XOR delta (REMAP.ASM, XORDELTA.ASM, also __asm in DrawMisc.cpp → VC drawbuff.cpp, xordelta.cpp).
C. DrawMisc.cpp (__asm): Buffer_Clear, Buffer_Draw_Line, Buffer_Fill_Rect, Buffer_Get/Put_Pixel, Linear_Blit_To_Linear, Linear_Scale_To_Linear, Buffer_Remap, Buffer_To_Page, Buffer_Draw_Stamp(_Clip), Init_Stamps, LCW_Uncompress, Build_Fading_Table, Set_Font_Palette_Range, XOR delta, IconCacheClass. IRANDOM.CPP → VC irandom.cpp.
D. Declared in wwlib headers, no body: all audio (§3); Set_Shape_Buffer (INIT ×2; RM Shape.cpp:52, VC shape.cpp); Draw_Shape/Init_Priority_System/Get_Shape_* (unused?); Buffer_Fill_Quad (RA FILLQUAD.ASM); Buffer_Size_Of_Region (RA SZREGION.ASM); ::Draw_Mouse, Mouse_Shadow_Buffer, ASM_Set_Mouse_Cursor (VC common/wwmouse.cpp); Mem_Copy (36 uses; RM MiscAsm.cpp:31); Largest_Mem_Block; Calculate_CRC (RM MiscAsm.cpp:1496); Shake_Screen (RM MiscAsm.cpp:908); Reverse_Long/Reverse_LONG/Reverse_Word (RM MiscAsm.cpp:1268); Delay (VC delay.cpp); Convert_RGB_To_HSV/HSV_To_RGB (VC tiberiandawn/lib.cpp); Desired_Facing8/16 (RM MiscAsm.cpp:371/104); RLE_Uncompress (VC load.cpp); Pack_2_Plane (RA PACK2PLN.ASM); LCW_Compress (VC lcw.cpp); Copy_Palette (RA PALETTE.CPP); Buffer_Bitblit_To_LogicPage (WSA.CPP:633,827; VC wsa.cpp); Window_More_Ptr (VC windows.cpp); Keyboard_Attributes_Off (INIT); timer-interrupt functions (stub); GetCDClass/RedBookClass (stub); Stop_Profiler (stub); Set_Original_Video_Mode (stub); IconCacheClass::Restore (stub); WinModemClass/ModemRegistryEntryClass/Get_Registry_Sub_Key (stub); RawFileClass::Set_Buffer_Size (trivial).
E. Game-level externs without body: VQA_* (INTRO, CONQUER, WINSTUB, INIT → RA WINVQ, VC vqa*.cpp); MPlayerID_To_ColorIndex (CONQUER:1195, INI:772); Get_IP_Address (MPLAYER:400); Suspend_/Resume_Audio_Thread; Audio_Unzap/sosCODEC*; Recoil_Adjust (UNIT.CPP ×3, no body anywhere — check liveness).
