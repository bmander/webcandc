/*
** webcandc: minimal COM plumbing so the bundled DirectX 5 headers (wwlib/DDRAW.H,
** wwlib/DSOUND.H) declare their interfaces as ordinary C++ abstract classes.
** src/platform implements those interfaces on SDL2.
*/
#ifndef WEBCANDC_OBJBASE_H
#define WEBCANDC_OBJBASE_H

#include "windows.h"

typedef unsigned short WCHAR;
typedef WCHAR *LPWSTR;
typedef const WCHAR *LPCWSTR;
typedef WCHAR OLECHAR;
typedef OLECHAR *LPOLESTR;

#define interface struct
#define STDMETHODCALLTYPE
#define STDMETHOD(method) virtual HRESULT STDMETHODCALLTYPE method
#define STDMETHOD_(type, method) virtual type STDMETHODCALLTYPE method
#define STDMETHODIMP HRESULT STDMETHODCALLTYPE
#define STDMETHODIMP_(type) type STDMETHODCALLTYPE
#define PURE = 0
#define THIS_
#define THIS void
#define DECLARE_INTERFACE(iface) interface iface
#define DECLARE_INTERFACE_(iface, base) interface iface : public base

#define MAKE_HRESULT(sev, fac, code) ((HRESULT)(((unsigned long)(sev) << 31) | ((unsigned long)(fac) << 16) | ((unsigned long)(code))))
#define CO_E_NOTINITIALIZED 0x800401F0L
#define CLSCTX_ALL 0x17
#define CLSCTX_INPROC_SERVER 0x1

interface IUnknown
{
	virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObject) = 0;
	virtual ULONG STDMETHODCALLTYPE AddRef(void) = 0;
	virtual ULONG STDMETHODCALLTYPE Release(void) = 0;
	virtual ~IUnknown() {}
};
typedef IUnknown *LPUNKNOWN;

#ifdef __cplusplus
extern "C" {
#endif
HRESULT WINAPI CoInitialize(LPVOID reserved);
void WINAPI CoUninitialize(void);
HRESULT WINAPI CoCreateInstance(REFCLSID clsid, LPUNKNOWN outer, DWORD ctx, REFIID iid, LPVOID *ppv);
#ifdef __cplusplus
}
#endif

#endif
