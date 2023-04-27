#pragma once

#include "d3dx9.h"
#include "d3dx9_core.h"

// "d3dx9shader.h"

#define D3DXSHADER_OPTIMIZATION_LEVEL0            (1 << 14)
#define D3DXSHADER_OPTIMIZATION_LEVEL3            (1 << 15)



//----------------------------------------------------------------------------
// D3DXHANDLE:
// -----------
// Handle values used to efficiently reference shader and effect parameters.
// Strings can be used as handles.  However, handles are not always strings.
//----------------------------------------------------------------------------

#ifndef D3DXFX_LARGEADDRESS_HANDLE
typedef LPCSTR D3DXHANDLE;
#else
typedef UINT_PTR D3DXHANDLE;
#endif
typedef D3DXHANDLE* LPD3DXHANDLE;






//typedef HRESULT(WINAPI* PFN_D3DXAssembleShader)(LPCSTR pSrcData, UINT SrcDataLen, const D3DXMACRO* pDefines, LPD3DXINCLUDE pInclude, DWORD Flags, LPD3DXBUFFER* ppShader, LPD3DXBUFFER* ppErrorMsgs);
//typedef HRESULT(WINAPI* PFN_D3DXDisassembleShader)(
//	const DWORD* pShader, 
//	BOOL EnableColorCode, 
//	LPCSTR pComments, 
//	LPD3DXBUFFER* ppDisassembly);
//
//extern PFN_D3DXAssembleShader D3DXAssembleShader;
//extern PFN_D3DXDisassembleShader D3DXDisassembleShader;