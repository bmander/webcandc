/*
** webcandc: Winsock, DDEML and process creation -- all reporting "not
** available". The 1995 code already handles these failures (no TCP/IP
** stack, WChat not running), which is how internet play and the WChat link
** switch themselves off. Multiplayer over WebSockets would slot in here.
*/
#include <windows.h>
#include <string.h>

extern "C" {

int WINAPI WSAStartup(WORD, LPWSADATA data)
{
	if (data) memset(data, 0, sizeof(*data));
	return WSASYSNOTREADY;
}

int WINAPI WSACleanup(void) { return 0; }
int WINAPI WSAGetLastError(void) { return WSASYSNOTREADY; }
int WINAPI WSAAsyncSelect(SOCKET, HWND, UINT, long) { return SOCKET_ERROR; }
HANDLE WINAPI WSAAsyncGetHostByName(HWND, UINT, const char *, char *, int) { return NULL; }
HANDLE WINAPI WSAAsyncGetHostByAddr(HWND, UINT, const char *, int, int, char *, int) { return NULL; }
int WINAPI WSACancelAsyncRequest(HANDLE) { return SOCKET_ERROR; }
int WINAPI closesocket(SOCKET) { return 0; }

UINT WINAPI DdeInitialize(LPDWORD inst, PFNCALLBACK, DWORD, DWORD)
{
	if (inst) *inst = 0;
	return DMLERR_DLL_NOT_INITIALIZED;
}

BOOL WINAPI DdeUninitialize(DWORD) { return TRUE; }
HSZ WINAPI DdeCreateStringHandle(DWORD, LPCSTR, int) { return NULL; }
BOOL WINAPI DdeFreeStringHandle(DWORD, HSZ) { return TRUE; }

DWORD WINAPI DdeQueryString(DWORD, HSZ, LPSTR buf, DWORD max, int)
{
	if (buf && max) buf[0] = '\0';
	return 0;
}

HDDEDATA WINAPI DdeNameService(DWORD, HSZ, HSZ, UINT) { return NULL; }
HCONV WINAPI DdeConnect(DWORD, HSZ, HSZ, void *) { return NULL; }
BOOL WINAPI DdeDisconnect(HCONV) { return TRUE; }
HDDEDATA WINAPI DdeClientTransaction(LPBYTE, DWORD, HCONV, HSZ, UINT, UINT, DWORD, LPDWORD result) { if (result) *result = 0; return NULL; }
LPBYTE WINAPI DdeAccessData(HDDEDATA, LPDWORD size) { if (size) *size = 0; return NULL; }
BOOL WINAPI DdeUnaccessData(HDDEDATA) { return TRUE; }
BOOL WINAPI DdeFreeDataHandle(HDDEDATA) { return TRUE; }

BOOL WINAPI CreateProcess(LPCSTR, LPSTR, LPSECURITY_ATTRIBUTES, LPSECURITY_ATTRIBUTES, BOOL, DWORD, LPVOID, LPCSTR, LPSTARTUPINFO, LPPROCESS_INFORMATION pi)
{
	if (pi) memset(pi, 0, sizeof(*pi));
	return FALSE;
}

LONG WINAPI RegQueryValue(HKEY, LPCSTR, LPSTR value, LONG *size)
{
	if (value && size && *size > 0) value[0] = '\0';
	return ERROR_FILE_NOT_FOUND;
}

}
