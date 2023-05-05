#include "../dxvk/dxvk_instance.h"

#include "d3d9_interface.h"
#include "d3d9_shader_validator.h"
#include "d3d9_device.h"

#define MGE_XE

#ifdef MGE_XE
#include "mge/configuration.h"
#include "mge/mgeversion.h"
#include "mwse/mgebridge.h"
#endif // MGE_XE

#include <windows.h>
#include <TlHelp32.h>
#include <Psapi.h>

class D3DFE_PROCESSVERTICES;
using PSGPERRORID = UINT;

#ifdef MGE_XE
static const char* welcomeMessage = XE_VERSION_STRING;
static bool isMW;
#endif // MGE_XE



namespace dxvk {
  Logger Logger::s_instance("d3d9.log");

  HRESULT CreateD3D9(
          bool           Extended,
          IDirect3D9Ex** ppDirect3D9Ex) {
    if (!ppDirect3D9Ex)
      return D3DERR_INVALIDCALL;

    *ppDirect3D9Ex = ref(new D3D9InterfaceEx( Extended ));
    return D3D_OK;
  }
}

#ifdef MGE_XE

extern "C" BOOL _stdcall DllMain(HANDLE hModule, DWORD reason, void* unused) {
  if (reason != DLL_PROCESS_ATTACH) {
    return true;
  }

  if (!Configuration.LoadSettings()) {
    dxvk::Logger::err("Error: MGE XE is not configured. MGE XE will be disabled for this session.");
    isMW = false;
    return true;
  }

  // Check if MW is in-process, avoid hooking the CS
  bool isMW = bool(GetModuleHandle("Morrowind.exe"));
  if (isMW) {
    dxvk::Logger::info("mgeXE.log");
    dxvk::Logger::info(welcomeMessage);

    if (!Configuration.LoadSettings()) {
      dxvk::Logger::err("Error: MGE XE is not configured. MGE XE will be disabled for this session.");
      isMW = false;
      return true;
    }

    if (~Configuration.MGEFlags & MWSE_DISABLED) {
      // Load MWSE dll, it injects by itself
      HMODULE dll = LoadLibraryA("MWSE.dll");
      if (dll) {
        // MWSE-MGE integration
        MWSE_MGEPlugin::init(dll);
        dxvk::Logger::info("MWSE.dll injected.");
      } else {
        dxvk::Logger::err("MWSE.dll failed to load.");
      }
    }
  }

  return true;
}
#endif // MGE_XE

extern "C" {

  DLLEXPORT IDirect3D9* __stdcall Direct3DCreate9(UINT nSDKVersion) {
    IDirect3D9Ex* pDirect3D = nullptr;
    dxvk::CreateD3D9(false, &pDirect3D);

    return pDirect3D;
  }

  DLLEXPORT HRESULT __stdcall Direct3DCreate9Ex(UINT nSDKVersion, IDirect3D9Ex** ppDirect3D9Ex) {
    return dxvk::CreateD3D9(true, ppDirect3D9Ex);
  }

  DLLEXPORT int __stdcall D3DPERF_BeginEvent(D3DCOLOR col, LPCWSTR wszName) {
    return 0;
  }

  DLLEXPORT int __stdcall D3DPERF_EndEvent(void) {
    return 0;
  }

  DLLEXPORT void __stdcall D3DPERF_SetMarker(D3DCOLOR col, LPCWSTR wszName) {
  }

  DLLEXPORT void __stdcall D3DPERF_SetRegion(D3DCOLOR col, LPCWSTR wszName) {
  }

  DLLEXPORT BOOL __stdcall D3DPERF_QueryRepeatFrame(void) {
    return FALSE;
  }

  DLLEXPORT void __stdcall D3DPERF_SetOptions(DWORD dwOptions) {
  }

  DLLEXPORT DWORD __stdcall D3DPERF_GetStatus(void) {
    return 0;
  }


  DLLEXPORT void __stdcall DebugSetMute(void) {
  }

  DLLEXPORT int __stdcall DebugSetLevel(void) {
    return 0;
  }

  // Processor Specific Geometry Pipeline
  // for P3 SIMD/AMD 3DNow.

  DLLEXPORT void __stdcall PSGPError(D3DFE_PROCESSVERTICES* a, PSGPERRORID b, UINT c) {
  }

  DLLEXPORT void __stdcall PSGPSampleTexture(D3DFE_PROCESSVERTICES* a, UINT b, float(*const c)[4], UINT d, float(*const e)[4]) {
  }

  DLLEXPORT dxvk::D3D9ShaderValidator* __stdcall Direct3DShaderValidatorCreate9(void) {
    return ref(new dxvk::D3D9ShaderValidator());
  }

  DLLEXPORT int __stdcall Direct3D9EnableMaximizedWindowedModeShim(UINT a) {
    return 0;
  }
}
