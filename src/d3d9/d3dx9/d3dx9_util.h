#pragma once

#include "d3dx9.h"
#include "d3dx9_math.h"
#include "d3dx9_shader.h"
#include "d3dx9_core.h"
#include "d3dx9_effect.h"
#include "d3dx9_tex.h"

#include <d3dcommon.h>

//namespace dxvk {

    // effect

    HRESULT D3DXCreateEffectPool(
        LPD3DXEFFECTPOOL* ppPool);

    HRESULT D3DXCreateEffectFromFile(
        LPDIRECT3DDEVICE9 pDevice,
        LPCSTR pSrcFile,
        const D3DXMACRO* pDefines,
        LPD3DXINCLUDE pInclude,
        DWORD                           Flags,
        LPD3DXEFFECTPOOL pPool,
        LPD3DXEFFECT* ppEffect,
        LPD3DXBUFFER* ppCompilationErrors);

    // shader

    HRESULT D3DXAssembleShader(
        LPCSTR pSrcData,
        UINT SrcDataLen,
        const D3DXMACRO* pDefines,
        LPD3DXINCLUDE pInclude,
        DWORD Flags,
        LPD3DXBUFFER* ppShader,
        LPD3DXBUFFER* ppErrorMsgs);

    HRESULT D3DXDisassembleShader(
        const DWORD* pShader,
        BOOL EnableColorCode,
        LPCSTR pComments,
        LPD3DXBUFFER* ppDisassembly);

    // tex

    HRESULT D3DXLoadSurfaceFromSurface(
        LPDIRECT3DSURFACE9 pDestSurface,
        const PALETTEENTRY* pDestPalette,
        const RECT* pDestRect,
        LPDIRECT3DSURFACE9 pSrcSurface,
        const PALETTEENTRY* pSrcPalette,
        const RECT* pSrcRect,
        DWORD Filter,
        D3DCOLOR ColorKey);

    HRESULT D3DXCreateVolumeTextureFromFile(
        LPDIRECT3DDEVICE9 pDevice,
        LPCSTR pSrcFile,
        LPDIRECT3DVOLUMETEXTURE9* ppVolumeTexture);

    HRESULT D3DXCreateTextureFromFileEx(
        LPDIRECT3DDEVICE9 pDevice,
        LPCSTR pSrcFile,
        UINT Width,
        UINT Height,
        UINT MipLevels,
        DWORD Usage,
        D3DFORMAT Format,
        D3DPOOL   Pool,
        DWORD Filter,
        DWORD MipFilter,
        D3DCOLOR  ColorKey,
        D3DXIMAGE_INFO* pSrcInfo,
        PALETTEENTRY* pPalette,
        LPDIRECT3DTEXTURE9* ppTexture);

    HRESULT D3DXSaveSurfaceToFile(
        LPCSTR pDestFile,
        D3DXIMAGE_FILEFORMAT DestFormat,
        LPDIRECT3DSURFACE9 pSrcSurface,
        CONST PALETTEENTRY* pSrcPalette,
        CONST RECT* pSrcRect);

    HRESULT D3DXCreateTextureFromFileInMemoryEx(
        LPDIRECT3DDEVICE9 pDevice,
        LPCVOID   pSrcData,
        UINT SrcDataSize,
        UINT Width,
        UINT Height,
        UINT MipLevels,
        DWORD Usage,
        D3DFORMAT Format,
        D3DPOOL   Pool,
        DWORD Filter,
        DWORD MipFilter,
        D3DCOLOR  ColorKey,
        D3DXIMAGE_INFO* pSrcInfo,
        PALETTEENTRY* pPalette,
        LPDIRECT3DTEXTURE9* ppTexture);
//}