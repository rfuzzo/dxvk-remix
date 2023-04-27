#include "d3dx9_util.h"


//namespace dxvk {

  // effect

    typedef HRESULT(WINAPI* PFN_D3DXCreateEffectPool)(LPD3DXEFFECTPOOL* ppPool);
    typedef HRESULT(STDMETHODCALLTYPE* PFN_D3DXCreateEffectFromFileA) (
        LPDIRECT3DDEVICE9 pDevice,
        LPCSTR pSrcFile,
        const D3DXMACRO* pDefines,
        LPD3DXINCLUDE pInclude,
        DWORD      Flags,
        LPD3DXEFFECTPOOL pPool,
        LPD3DXEFFECT* ppEffect,
        LPD3DXBUFFER* ppCompilationErrors);

    PFN_D3DXCreateEffectPool g_pfnCreateEffectPool = nullptr;
    PFN_D3DXCreateEffectFromFileA g_pfnCreateEffectFromFile = nullptr;

    HRESULT D3DXCreateEffectPool(LPD3DXEFFECTPOOL* ppPool)
    {
        if (g_pfnCreateEffectPool == nullptr) {
            HMODULE d3d9x = LoadLibraryA("d3dx9.dll");

            if (d3d9x == nullptr)
                d3d9x = LoadLibraryA("d3dx9_43.dll");

            g_pfnCreateEffectPool =
                reinterpret_cast<PFN_D3DXCreateEffectPool>(GetProcAddress(d3d9x, "D3DXCreateEffectPool"));
        }

        if (g_pfnCreateEffectPool == nullptr)
            return D3DERR_INVALIDCALL;

        return g_pfnCreateEffectPool(ppPool);
    }

  HRESULT D3DXCreateEffectFromFile(
      LPDIRECT3DDEVICE9 pDevice,
      LPCSTR pSrcFile,
      const D3DXMACRO* pDefines,
      LPD3DXINCLUDE pInclude,
      DWORD      Flags,
      LPD3DXEFFECTPOOL pPool,
      LPD3DXEFFECT* ppEffect,
      LPD3DXBUFFER* ppCompilationErrors) {
    if (g_pfnCreateEffectFromFile == nullptr) {
      HMODULE d3d9x = LoadLibraryA("d3dx9.dll");

      if (d3d9x == nullptr)
        d3d9x = LoadLibraryA("d3dx9_43.dll");

      g_pfnCreateEffectFromFile =
        reinterpret_cast<PFN_D3DXCreateEffectFromFileA>(GetProcAddress(d3d9x, "D3DXCreateEffectFromFileA"));
    }

    if (g_pfnCreateEffectFromFile == nullptr)
      return D3DERR_INVALIDCALL;

    return g_pfnCreateEffectFromFile(
      pDevice,
      pSrcFile,
      pDefines,
      pInclude,
      Flags,
      pPool,
      ppEffect,
      ppCompilationErrors);
  }

  // shader

  typedef HRESULT(WINAPI* PFN_D3DXAssembleShader)(
      LPCSTR pSrcData, 
      UINT SrcDataLen, 
      const D3DXMACRO* pDefines, 
      LPD3DXINCLUDE pInclude, 
      DWORD Flags, 
      LPD3DXBUFFER* ppShader, 
      LPD3DXBUFFER* ppErrorMsgs);
  typedef HRESULT(WINAPI* PFN_D3DXDisassembleShader)(
      const DWORD* pShader,
      BOOL EnableColorCode,
      LPCSTR pComments,
      LPD3DXBUFFER* ppDisassembly);

  PFN_D3DXAssembleShader g_pfnAssembleShader = nullptr;
  PFN_D3DXDisassembleShader g_pfnDisassembleShader = nullptr;

  HRESULT D3DXAssembleShader(
      LPCSTR pSrcData,
      UINT SrcDataLen,
      const D3DXMACRO* pDefines,
      LPD3DXINCLUDE pInclude,
      DWORD Flags,
      LPD3DXBUFFER* ppShader,
      LPD3DXBUFFER* ppErrorMsgs)
  {
      if (g_pfnAssembleShader == nullptr) {
          HMODULE d3d9x = LoadLibraryA("d3dx9.dll");

          if (d3d9x == nullptr)
              d3d9x = LoadLibraryA("d3dx9_43.dll");

          g_pfnAssembleShader =
              reinterpret_cast<PFN_D3DXAssembleShader>(GetProcAddress(d3d9x, "D3DXAssembleShader"));
      }

      if (g_pfnAssembleShader == nullptr)
          return D3DERR_INVALIDCALL;

      return g_pfnAssembleShader(
           pSrcData,
           SrcDataLen,
           pDefines,
          pInclude,
          Flags,
          ppShader,
          ppErrorMsgs);
  }

  HRESULT D3DXDisassembleShader(
      const DWORD* pShader,
      BOOL EnableColorCode,
      LPCSTR pComments,
      LPD3DXBUFFER* ppDisassembly)
  {
      if (g_pfnDisassembleShader == nullptr) {
          HMODULE d3d9x = LoadLibraryA("d3dx9.dll");

          if (d3d9x == nullptr)
              d3d9x = LoadLibraryA("d3dx9_43.dll");

          g_pfnDisassembleShader =
              reinterpret_cast<PFN_D3DXDisassembleShader>(GetProcAddress(d3d9x, "D3DXDisassembleShader"));
      }

      if (g_pfnDisassembleShader == nullptr)
          return D3DERR_INVALIDCALL;

      return g_pfnDisassembleShader(
          pShader,
          EnableColorCode,
          pComments,
          ppDisassembly);
  }

  // tex

  typedef HRESULT(WINAPI* PFN_D3DXLoadSurfaceFromSurface)(
      LPDIRECT3DSURFACE9 pDestSurface, 
      const PALETTEENTRY* pDestPalette, 
      const RECT* pDestRect, 
      LPDIRECT3DSURFACE9 pSrcSurface, 
      const PALETTEENTRY* pSrcPalette, 
      const RECT* pSrcRect, 
      DWORD Filter, 
      D3DCOLOR ColorKey);
  typedef HRESULT(WINAPI* PFN_D3DXCreateVolumeTextureFromFileA)(
      LPDIRECT3DDEVICE9         pDevice,
      LPCSTR    pSrcFile,
      LPDIRECT3DVOLUMETEXTURE9* ppVolumeTexture);
  typedef HRESULT(WINAPI* PFN_D3DXCreateTextureFromFileExA)(
      LPDIRECT3DDEVICE9         pDevice,
      LPCSTR    pSrcFile,
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
  typedef HRESULT(WINAPI* PFN_D3DXSaveSurfaceToFileA)(
      LPCSTR    pDestFile,
      D3DXIMAGE_FILEFORMAT      DestFormat,
      LPDIRECT3DSURFACE9        pSrcSurface,
      CONST PALETTEENTRY* pSrcPalette,
      CONST RECT* pSrcRect);
  typedef HRESULT(WINAPI* PFN_D3DXCreateTextureFromFileInMemoryEx)(
      LPDIRECT3DDEVICE9         pDevice,
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


    PFN_D3DXLoadSurfaceFromSurface g_pfnLoadSurfaceFromSurface = nullptr;
    PFN_D3DXCreateVolumeTextureFromFileA g_pfnCreateVolumeTextureFromFile = nullptr;
    PFN_D3DXCreateTextureFromFileExA g_pfnCreateTextureFromFileEx = nullptr;
    PFN_D3DXSaveSurfaceToFileA g_pfnSaveSurfaceToFile = nullptr;
    PFN_D3DXCreateTextureFromFileInMemoryEx g_pfnCreateTextureFromFileInMemoryEx = nullptr;

    HRESULT D3DXLoadSurfaceFromSurface(
        LPDIRECT3DSURFACE9 pDestSurface,
        const PALETTEENTRY* pDestPalette,
        const RECT* pDestRect,
        LPDIRECT3DSURFACE9 pSrcSurface,
        const PALETTEENTRY* pSrcPalette,
        const RECT* pSrcRect,
        DWORD Filter,
        D3DCOLOR ColorKey)
    {
        if (g_pfnLoadSurfaceFromSurface == nullptr) {
            HMODULE d3d9x = LoadLibraryA("d3dx9.dll");

            if (d3d9x == nullptr)
                d3d9x = LoadLibraryA("d3dx9_43.dll");

            g_pfnLoadSurfaceFromSurface =
                reinterpret_cast<PFN_D3DXLoadSurfaceFromSurface>(GetProcAddress(d3d9x, "D3DXLoadSurfaceFromSurface"));
        }

        if (g_pfnLoadSurfaceFromSurface == nullptr)
            return D3DERR_INVALIDCALL;

        return g_pfnLoadSurfaceFromSurface(
            pDestSurface,
            pDestPalette,
            pDestRect,
            pSrcSurface,
            pSrcPalette,
            pSrcRect,
            Filter,
            ColorKey);
    }

    HRESULT D3DXCreateVolumeTextureFromFile(
        LPDIRECT3DDEVICE9 pDevice,
        LPCSTR pSrcFile,
        LPDIRECT3DVOLUMETEXTURE9* ppVolumeTexture)
    {
        if (g_pfnCreateVolumeTextureFromFile == nullptr) {
            HMODULE d3d9x = LoadLibraryA("d3dx9.dll");

            if (d3d9x == nullptr)
                d3d9x = LoadLibraryA("d3dx9_43.dll");

            g_pfnCreateVolumeTextureFromFile =
                reinterpret_cast<PFN_D3DXCreateVolumeTextureFromFileA>(GetProcAddress(d3d9x, "CreateVolumeTextureFromFileA"));
        }

        if (g_pfnCreateVolumeTextureFromFile == nullptr)
            return D3DERR_INVALIDCALL;

        return g_pfnCreateVolumeTextureFromFile(
            pDevice,
            pSrcFile,
            ppVolumeTexture);
    }

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
        LPDIRECT3DTEXTURE9* ppTexture)
    {
        if (g_pfnCreateTextureFromFileEx == nullptr) {
            HMODULE d3d9x = LoadLibraryA("d3dx9.dll");

            if (d3d9x == nullptr)
                d3d9x = LoadLibraryA("d3dx9_43.dll");

            g_pfnCreateTextureFromFileEx =
                reinterpret_cast<PFN_D3DXCreateTextureFromFileExA>(GetProcAddress(d3d9x, "D3DXCreateTextureFromFileExA"));
        }

        if (g_pfnCreateTextureFromFileEx == nullptr)
            return D3DERR_INVALIDCALL;

        return g_pfnCreateTextureFromFileEx(
            pDevice,
            pSrcFile,
            Width,
            Height,
            MipLevels,
            Usage,
            Format,
            Pool,
            Filter,
            MipFilter,
            ColorKey,
            pSrcInfo,
            pPalette,
            ppTexture);
    }

    HRESULT D3DXSaveSurfaceToFile(
        LPCSTR pDestFile,
        D3DXIMAGE_FILEFORMAT DestFormat,
        LPDIRECT3DSURFACE9 pSrcSurface,
        CONST PALETTEENTRY* pSrcPalette,
        CONST RECT* pSrcRect)
    {
        if (g_pfnSaveSurfaceToFile == nullptr) {
            HMODULE d3d9x = LoadLibraryA("d3dx9.dll");

            if (d3d9x == nullptr)
                d3d9x = LoadLibraryA("d3dx9_43.dll");

            g_pfnSaveSurfaceToFile =
                reinterpret_cast<PFN_D3DXSaveSurfaceToFileA>(GetProcAddress(d3d9x, "D3DXSaveSurfaceToFileA"));
        }

        if (g_pfnSaveSurfaceToFile == nullptr)
            return D3DERR_INVALIDCALL;

        return g_pfnSaveSurfaceToFile(
            pDestFile,
            DestFormat,
            pSrcSurface,
            pSrcPalette,
            pSrcRect);
    }

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
        LPDIRECT3DTEXTURE9* ppTexture)
    {
        if (g_pfnCreateTextureFromFileInMemoryEx == nullptr) {
            HMODULE d3d9x = LoadLibraryA("d3dx9.dll");

            if (d3d9x == nullptr)
                d3d9x = LoadLibraryA("d3dx9_43.dll");

            g_pfnCreateTextureFromFileInMemoryEx =
                reinterpret_cast<PFN_D3DXCreateTextureFromFileInMemoryEx>(GetProcAddress(d3d9x, "D3DXCreateTextureFromFileInMemoryEx"));
        }

        if (g_pfnCreateTextureFromFileInMemoryEx == nullptr)
            return D3DERR_INVALIDCALL;

        return g_pfnCreateTextureFromFileInMemoryEx(
            pDevice,
            pSrcData,
            SrcDataSize,
            Width,
            Height,
            MipLevels,
            Usage,
            Format,
            Pool,
            Filter,
            MipFilter,
            ColorKey,
            pSrcInfo,
            pPalette,
            ppTexture);
    }

//}