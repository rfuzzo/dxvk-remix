#pragma once

#include "d3dx9.h"


// "d3dx9tex.h"

//----------------------------------------------------------------------------
// D3DXIMAGE_FILEFORMAT:
// ---------------------
// This enum is used to describe supported image file formats.
//
//----------------------------------------------------------------------------

typedef enum _D3DXIMAGE_FILEFORMAT
{
    D3DXIFF_BMP = 0,
    D3DXIFF_JPG = 1,
    D3DXIFF_TGA = 2,
    D3DXIFF_PNG = 3,
    D3DXIFF_DDS = 4,
    D3DXIFF_PPM = 5,
    D3DXIFF_DIB = 6,
    D3DXIFF_HDR = 7,       //high dynamic range formats
    D3DXIFF_PFM = 8,       //
    D3DXIFF_FORCE_DWORD = 0x7fffffff

} D3DXIMAGE_FILEFORMAT;

//----------------------------------------------------------------------------
// D3DXIMAGE_INFO:

typedef struct _D3DXIMAGE_INFO
{
    UINT                    Width;
    UINT                    Height;
    UINT                    Depth;
    UINT                    MipLevels;
    D3DFORMAT               Format;
    D3DRESOURCETYPE         ResourceType;
    D3DXIMAGE_FILEFORMAT    ImageFileFormat;

} D3DXIMAGE_INFO;












//typedef HRESULT(WINAPI* PFN_D3DXLoadSurfaceFromSurface)(
// LPDIRECT3DSURFACE9 pDestSurface, 
// const PALETTEENTRY* pDestPalette, 
// const RECT* pDestRect, 
// LPDIRECT3DSURFACE9 pSrcSurface, 
// const PALETTEENTRY* pSrcPalette, 
// const RECT* pSrcRect, 
// DWORD Filter, 
// D3DCOLOR ColorKey);
//typedef HRESULT(WINAPI* PFN_D3DXCreateVolumeTextureFromFileA)(
//    LPDIRECT3DDEVICE9         pDevice,
//    LPCSTR                    pSrcFile,
//    LPDIRECT3DVOLUMETEXTURE9* ppVolumeTexture);
//typedef HRESULT(WINAPI* PFN_D3DXCreateTextureFromFileExA)(
//    LPDIRECT3DDEVICE9         pDevice,
//    LPCSTR                    pSrcFile,
//    UINT                      Width,
//    UINT                      Height,
//    UINT                      MipLevels,
//    DWORD                     Usage,
//    D3DFORMAT                 Format,
//    D3DPOOL                   Pool,
//    DWORD                     Filter,
//    DWORD                     MipFilter,
//    D3DCOLOR                  ColorKey,
//    D3DXIMAGE_INFO* pSrcInfo,
//    PALETTEENTRY* pPalette,
//    LPDIRECT3DTEXTURE9* ppTexture);
//typedef HRESULT(WINAPI* PFN_D3DXSaveSurfaceToFileA)(
//    LPCSTR                    pDestFile,
//    D3DXIMAGE_FILEFORMAT      DestFormat,
//    LPDIRECT3DSURFACE9        pSrcSurface,
//    CONST PALETTEENTRY* pSrcPalette,
//    CONST RECT* pSrcRect);
//typedef HRESULT(WINAPI* PFN_D3DXCreateTextureFromFileInMemoryEx)(
//    LPDIRECT3DDEVICE9         pDevice,
//    LPCVOID                   pSrcData,
//    UINT                      SrcDataSize,
//    UINT                      Width,
//    UINT                      Height,
//    UINT                      MipLevels,
//    DWORD                     Usage,
//    D3DFORMAT                 Format,
//    D3DPOOL                   Pool,
//    DWORD                     Filter,
//    DWORD                     MipFilter,
//    D3DCOLOR                  ColorKey,
//    D3DXIMAGE_INFO* pSrcInfo,
//    PALETTEENTRY* pPalette,
//    LPDIRECT3DTEXTURE9* ppTexture);


//
//extern PFN_D3DXLoadSurfaceFromSurface D3DXLoadSurfaceFromSurface;
//extern PFN_D3DXCreateVolumeTextureFromFileA D3DXCreateVolumeTextureFromFile;
//extern PFN_D3DXCreateTextureFromFileExA D3DXCreateTextureFromFileEx;
//extern PFN_D3DXSaveSurfaceToFileA D3DXSaveSurfaceToFile;
//extern PFN_D3DXCreateTextureFromFileInMemoryEx D3DXCreateTextureFromFileInMemoryEx;






