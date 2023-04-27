#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <d3d9.h>

#define D3DX_DEFAULT            ((UINT) -1)
#define D3DX_FROM_FILE          ((UINT) -3)

#define D3DX_FILTER_NONE 1

#define D3DXASM_DEBUG 0x0001
#define D3DXASM_SKIPVALIDATION  0x0010

#ifdef NDEBUG
#define D3DXASM_FLAGS  0
#else
#define D3DXASM_FLAGS D3DXASM_DEBUG
#endif // NDEBUG

#ifndef D3DXINLINE
#ifdef _MSC_VER
#if (_MSC_VER >= 1200)
#define D3DXINLINE __forceinline
#else
#define D3DXINLINE __inline
#endif
#else
#ifdef __cplusplus
#define D3DXINLINE inline
#else
#define D3DXINLINE
#endif
#endif
#endif

struct D3DXMACRO
{
	LPCSTR Name;
	LPCSTR Definition;
};

