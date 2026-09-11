/*
** webcandc: stand-ins for pieces of the 1995 build that were never released
** or depend on hardware a browser doesn't have:
**   - Greenleaf CommLib serial/modem play (NULLMGR/NULLDLG): no modem found.
**   - THIPX32.DLL (the Win95 IPX thunk behind IPX95.CPP): no IPX network.
**   - A few globals that lived in Watcom's runtime or the Remastered DLL.
*/
#include "../game/FUNCTION.H"	// the game's master header (wwlib has its own FUNCTION.H)

/*
** ---- Greenleaf null-modem connection manager --------------------------------
*/
NullModemClass::NullModemClass(int, int, int, unsigned short) {}
NullModemClass::~NullModemClass() {}
int NullModemClass::Num_Connections(void) { return 0; }
int NullModemClass::Init_Send_Queue(void) { return 0; }
void NullModemClass::Set_Timing(unsigned long, unsigned long, unsigned long) {}
int NullModemClass::Send_Message(void *, int, int) { return 0; }
int NullModemClass::Get_Message(void *, int *buflen) { if (buflen) *buflen = 0; return 0; }
int NullModemClass::Service(void) { return 0; }
int NullModemClass::Num_Send(void) { return 0; }
int NullModemClass::Num_Receive(void) { return 0; }
unsigned long NullModemClass::Response_Time(void) { return 0; }
void NullModemClass::Reset_Response_Time(void) {}
void NullModemClass::Configure_Debug(int, int, int, char **, int) {}
void NullModemClass::Mono_Debug_Print(int, int) {}
int NullModemClass::Change_IRQ_Priority(int) { return 0; }

/*
** ---- Serial/modem dialogs (NULLDLG.CPP) -----------------------------------------
*/
void Smart_Printf(char *, ...) {}
void Hex_Dump_Data(char *, int) {}
void Modem_Signoff(void) {}
void Shutdown_Modem(void) {}
int Reconnect_Modem(void) { return 0; }
void Destroy_Null_Connection(int, int) {}
GameType Select_Serial_Dialog(void) { return GAME_NORMAL; }
int Com_Scenario_Dialog(void) { return 0; }
int Com_Show_Scenario_Dialog(void) { return 0; }

/*
** ---- THIPX32.DLL: report no IPX network -------------------------------------------
*/
extern "C" {
BOOL __stdcall IPX_Initialise(void) { return FALSE; }
BOOL __stdcall IPX_Get_Outstanding_Buffer95(unsigned char *) { return FALSE; }
void __stdcall IPX_Shut_Down95(void) {}
int __stdcall IPX_Send_Packet95(unsigned char *, unsigned char *, int, unsigned char *, unsigned char *) { return 0; }
int __stdcall IPX_Broadcast_Packet95(unsigned char *, int) { return 0; }
BOOL __stdcall IPX_Start_Listening95(void) { return FALSE; }
int __stdcall IPX_Open_Socket95(int) { return -1; }
void __stdcall IPX_Close_Socket95(int) {}
int __stdcall IPX_Get_Connection_Number95(void) { return 0; }
int __stdcall IPX_Get_Local_Target95(unsigned char *, unsigned char *, unsigned short, unsigned char *) { return 0; }
}

/*
** ---- Registry walk used by INTERNET.CPP -----------------------------------------------
*/
HKEY Get_Registry_Sub_Key(HKEY, char *, BOOL) { return NULL; }

/*
** ---- Runtime / DLL globals ------------------------------------------------------------
*/
extern "C" char CPUType = 5;		// Pentium, as reported in the statistics packet.
WORD Hard_Error_Occured = 0;		// Set by the DOS critical-error handler.
int DLLForceMouseX = -1;			// Remastered DLL override of the mouse position; unused.
int DLLForceMouseY = -1;
char *__nheapbeg = NULL;			// Watcom near-heap start, walked by a debug heap dump.
