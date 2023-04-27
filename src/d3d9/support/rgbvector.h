#pragma once

#include "d3dx9/d3dx9_math.h"
#include <d3d9.h>

typedef struct RGBVECTOR {
public:
    RGBVECTOR() {}
    RGBVECTOR( DWORD rgb );
    RGBVECTOR( CONST FLOAT*);
    RGBVECTOR( CONST D3DXFLOAT16*);
    RGBVECTOR( CONST RGBVECTOR*);
    RGBVECTOR( CONST RGBVECTOR&);
    RGBVECTOR( CONST D3DCOLORVALUE&);
    RGBVECTOR( CONST D3DXCOLOR&);
    RGBVECTOR( FLOAT r, FLOAT g, FLOAT b );

    // casting
    operator DWORD () const;

    operator FLOAT* ();
    operator CONST FLOAT* () const;

    // assignment operators
    RGBVECTOR& operator += (CONST RGBVECTOR&);
    RGBVECTOR& operator -= (CONST RGBVECTOR&);
    RGBVECTOR& operator += (CONST D3DXCOLOR&);
    RGBVECTOR& operator -= (CONST D3DXCOLOR&);
    RGBVECTOR& operator *= (FLOAT);
    RGBVECTOR& operator /= (FLOAT);

    // unary operators
    RGBVECTOR operator + () const;
    RGBVECTOR operator - () const;

    // binary operators
    RGBVECTOR operator + (CONST RGBVECTOR&) const;
    RGBVECTOR operator - (CONST RGBVECTOR&) const;
    RGBVECTOR operator + (CONST D3DXCOLOR&) const;
    RGBVECTOR operator - (CONST D3DXCOLOR&) const;
    RGBVECTOR operator * (FLOAT) const;
    RGBVECTOR operator / (FLOAT) const;

    friend RGBVECTOR operator * (FLOAT, CONST RGBVECTOR&);

    BOOL operator == (CONST RGBVECTOR&) const;
    BOOL operator != (CONST RGBVECTOR&) const;
    BOOL operator == (CONST D3DXCOLOR&) const;
    BOOL operator != (CONST D3DXCOLOR&) const;

    FLOAT r, g, b;
} RGBVECTOR, *LPRGBVECTOR;

// RGBVECTOR method definitions
#include "rgbvector.inl"

