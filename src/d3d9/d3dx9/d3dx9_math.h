#pragma once

#include "d3dx9.h"
#include <math.h>

// "d3dx9math.h"

//===========================================================================
//
// General purpose utilities
//
//===========================================================================
#define D3DX_PI    ((FLOAT)  3.141592654f)

//===========================================================================
//
// 16 bit floating point numbers
//
//===========================================================================

#define D3DX_16F_DIG          3                // # of decimal digits of precision
#define D3DX_16F_EPSILON      4.8875809e-4f    // smallest such that 1.0 + epsilon != 1.0
#define D3DX_16F_MANT_DIG     11               // # of bits in mantissa
#define D3DX_16F_MAX          6.550400e+004    // max value
#define D3DX_16F_MAX_10_EXP   4                // max decimal exponent
#define D3DX_16F_MAX_EXP      15               // max binary exponent
#define D3DX_16F_MIN          6.1035156e-5f    // min positive value
#define D3DX_16F_MIN_10_EXP   (-4)             // min decimal exponent
#define D3DX_16F_MIN_EXP      (-14)            // min binary exponent
#define D3DX_16F_RADIX        2                // exponent radix
#define D3DX_16F_ROUNDS       1                // addition rounding: near


typedef struct D3DXFLOAT16
{
#ifdef __cplusplus
public:
    D3DXFLOAT16() {};
    D3DXFLOAT16(FLOAT);
    D3DXFLOAT16(CONST D3DXFLOAT16&);

    // casting
    operator FLOAT ();

    // binary operators
    BOOL operator == (CONST D3DXFLOAT16&) const;
    BOOL operator != (CONST D3DXFLOAT16&) const;

protected:
#endif //__cplusplus
    WORD value;
} D3DXFLOAT16, * LPD3DXFLOAT16;

//===========================================================================
//
// Vectors
//
//===========================================================================


//--------------------------
// 2D Vector
//--------------------------
typedef struct D3DXVECTOR2
{
#ifdef __cplusplus
public:
    D3DXVECTOR2() {};
    D3DXVECTOR2(CONST FLOAT*);
    D3DXVECTOR2(CONST D3DXFLOAT16*);
    D3DXVECTOR2(FLOAT x, FLOAT y);

    // casting
    operator FLOAT* ();
    operator CONST FLOAT* () const;

    // assignment operators
    D3DXVECTOR2& operator += (CONST D3DXVECTOR2&);
    D3DXVECTOR2& operator -= (CONST D3DXVECTOR2&);
    D3DXVECTOR2& operator *= (FLOAT);
    D3DXVECTOR2& operator /= (FLOAT);

    // unary operators
    D3DXVECTOR2 operator + () const;
    D3DXVECTOR2 operator - () const;

    // binary operators
    D3DXVECTOR2 operator + (CONST D3DXVECTOR2&) const;
    D3DXVECTOR2 operator - (CONST D3DXVECTOR2&) const;
    D3DXVECTOR2 operator * (FLOAT) const;
    D3DXVECTOR2 operator / (FLOAT) const;

    friend D3DXVECTOR2 operator * (FLOAT, CONST D3DXVECTOR2&);

    BOOL operator == (CONST D3DXVECTOR2&) const;
    BOOL operator != (CONST D3DXVECTOR2&) const;


public:
#endif //__cplusplus
    FLOAT x, y;
} D3DXVECTOR2, * LPD3DXVECTOR2;

//--------------------------
// 3D Vector
//--------------------------
#ifdef __cplusplus
typedef struct D3DXVECTOR3 : public D3DVECTOR
{
public:
    D3DXVECTOR3() {};
    D3DXVECTOR3(CONST FLOAT*);
    D3DXVECTOR3(CONST D3DVECTOR&);
    D3DXVECTOR3(CONST D3DXFLOAT16*);
    D3DXVECTOR3(FLOAT x, FLOAT y, FLOAT z);

    // casting
    operator FLOAT* ();
    operator CONST FLOAT* () const;

    // assignment operators
    D3DXVECTOR3& operator += (CONST D3DXVECTOR3&);
    D3DXVECTOR3& operator -= (CONST D3DXVECTOR3&);
    D3DXVECTOR3& operator *= (FLOAT);
    D3DXVECTOR3& operator /= (FLOAT);

    // unary operators
    D3DXVECTOR3 operator + () const;
    D3DXVECTOR3 operator - () const;

    // binary operators
    D3DXVECTOR3 operator + (CONST D3DXVECTOR3&) const;
    D3DXVECTOR3 operator - (CONST D3DXVECTOR3&) const;
    D3DXVECTOR3 operator * (FLOAT) const;
    D3DXVECTOR3 operator / (FLOAT) const;

    friend D3DXVECTOR3 operator * (FLOAT, CONST struct D3DXVECTOR3&);

    BOOL operator == (CONST D3DXVECTOR3&) const;
    BOOL operator != (CONST D3DXVECTOR3&) const;

} D3DXVECTOR3, * LPD3DXVECTOR3;

#else //!__cplusplus
typedef struct _D3DVECTOR D3DXVECTOR3, * LPD3DXVECTOR3;
#endif //!__cplusplus


//--------------------------
// 4D Vector
//--------------------------
typedef struct D3DXVECTOR4
{
#ifdef __cplusplus
public:
    D3DXVECTOR4() {};
    D3DXVECTOR4(CONST FLOAT*);
    D3DXVECTOR4(CONST D3DXFLOAT16*);
    D3DXVECTOR4(CONST D3DVECTOR& xyz, FLOAT w);
    D3DXVECTOR4(FLOAT x, FLOAT y, FLOAT z, FLOAT w);

    // casting
    operator FLOAT* ();
    operator CONST FLOAT* () const;

    // assignment operators
    D3DXVECTOR4& operator += (CONST D3DXVECTOR4&);
    D3DXVECTOR4& operator -= (CONST D3DXVECTOR4&);
    D3DXVECTOR4& operator *= (FLOAT);
    D3DXVECTOR4& operator /= (FLOAT);

    // unary operators
    D3DXVECTOR4 operator + () const;
    D3DXVECTOR4 operator - () const;

    // binary operators
    D3DXVECTOR4 operator + (CONST D3DXVECTOR4&) const;
    D3DXVECTOR4 operator - (CONST D3DXVECTOR4&) const;
    D3DXVECTOR4 operator * (FLOAT) const;
    D3DXVECTOR4 operator / (FLOAT) const;

    friend D3DXVECTOR4 operator * (FLOAT, CONST D3DXVECTOR4&);

    BOOL operator == (CONST D3DXVECTOR4&) const;
    BOOL operator != (CONST D3DXVECTOR4&) const;

public:
#endif //__cplusplus
    FLOAT x, y, z, w;
} D3DXVECTOR4, * LPD3DXVECTOR4;

//===========================================================================
//
// Matrices
//
//===========================================================================
#ifdef __cplusplus
typedef struct D3DXMATRIX : public D3DMATRIX
{
public:
    D3DXMATRIX() {};
    D3DXMATRIX(CONST FLOAT*);
    D3DXMATRIX(CONST D3DMATRIX&);
    D3DXMATRIX(CONST D3DXFLOAT16*);
    D3DXMATRIX(FLOAT _11, FLOAT _12, FLOAT _13, FLOAT _14,
        FLOAT _21, FLOAT _22, FLOAT _23, FLOAT _24,
        FLOAT _31, FLOAT _32, FLOAT _33, FLOAT _34,
        FLOAT _41, FLOAT _42, FLOAT _43, FLOAT _44);


    // access grants
    FLOAT& operator () (UINT Row, UINT Col);
    FLOAT  operator () (UINT Row, UINT Col) const;

    // casting operators
    operator FLOAT* ();
    operator CONST FLOAT* () const;

    // assignment operators
    D3DXMATRIX& operator *= (CONST D3DXMATRIX&);
    D3DXMATRIX& operator += (CONST D3DXMATRIX&);
    D3DXMATRIX& operator -= (CONST D3DXMATRIX&);
    D3DXMATRIX& operator *= (FLOAT);
    D3DXMATRIX& operator /= (FLOAT);

    // unary operators
    D3DXMATRIX operator + () const;
    D3DXMATRIX operator - () const;

    // binary operators
    D3DXMATRIX operator * (CONST D3DXMATRIX&) const;
    D3DXMATRIX operator + (CONST D3DXMATRIX&) const;
    D3DXMATRIX operator - (CONST D3DXMATRIX&) const;
    D3DXMATRIX operator * (FLOAT) const;
    D3DXMATRIX operator / (FLOAT) const;

    friend D3DXMATRIX operator * (FLOAT, CONST D3DXMATRIX&);

    BOOL operator == (CONST D3DXMATRIX&) const;
    BOOL operator != (CONST D3DXMATRIX&) const;

} D3DXMATRIX, * LPD3DXMATRIX;

#else //!__cplusplus
typedef struct _D3DMATRIX D3DXMATRIX, * LPD3DXMATRIX;
#endif //!__cplusplus





//===========================================================================
//
// Planes
//
//===========================================================================
typedef struct D3DXPLANE
{
#ifdef __cplusplus
public:
    D3DXPLANE() {}
    D3DXPLANE(CONST FLOAT*);
    D3DXPLANE(CONST D3DXFLOAT16*);
    D3DXPLANE(FLOAT a, FLOAT b, FLOAT c, FLOAT d);

    // casting
    operator FLOAT* ();
    operator CONST FLOAT* () const;

    // assignment operators
    D3DXPLANE& operator *= (FLOAT);
    D3DXPLANE& operator /= (FLOAT);

    // unary operators
    D3DXPLANE operator + () const;
    D3DXPLANE operator - () const;

    // binary operators
    D3DXPLANE operator * (FLOAT) const;
    D3DXPLANE operator / (FLOAT) const;

    friend D3DXPLANE operator * (FLOAT, CONST D3DXPLANE&);

    BOOL operator == (CONST D3DXPLANE&) const;
    BOOL operator != (CONST D3DXPLANE&) const;

#endif //__cplusplus
    FLOAT a, b, c, d;
} D3DXPLANE, * LPD3DXPLANE;


//===========================================================================
//
// Colors
//
//===========================================================================

typedef struct D3DXCOLOR
{
#ifdef __cplusplus
public:
    D3DXCOLOR() {}
    D3DXCOLOR(DWORD argb);
    D3DXCOLOR(CONST FLOAT*);
    D3DXCOLOR(CONST D3DXFLOAT16*);
    D3DXCOLOR(CONST D3DCOLORVALUE&);
    D3DXCOLOR(FLOAT r, FLOAT g, FLOAT b, FLOAT a);

    // casting
    operator DWORD () const;

    operator FLOAT* ();
    operator CONST FLOAT* () const;

    operator D3DCOLORVALUE* ();
    operator CONST D3DCOLORVALUE* () const;

    operator D3DCOLORVALUE& ();
    operator CONST D3DCOLORVALUE& () const;

    // assignment operators
    D3DXCOLOR& operator += (CONST D3DXCOLOR&);
    D3DXCOLOR& operator -= (CONST D3DXCOLOR&);
    D3DXCOLOR& operator *= (FLOAT);
    D3DXCOLOR& operator /= (FLOAT);

    // unary operators
    D3DXCOLOR operator + () const;
    D3DXCOLOR operator - () const;

    // binary operators
    D3DXCOLOR operator + (CONST D3DXCOLOR&) const;
    D3DXCOLOR operator - (CONST D3DXCOLOR&) const;
    D3DXCOLOR operator * (FLOAT) const;
    D3DXCOLOR operator / (FLOAT) const;

    friend D3DXCOLOR operator * (FLOAT, CONST D3DXCOLOR&);

    BOOL operator == (CONST D3DXCOLOR&) const;
    BOOL operator != (CONST D3DXCOLOR&) const;

#endif //__cplusplus
    FLOAT r, g, b, a;
} D3DXCOLOR, * LPD3DXCOLOR;



//===========================================================================
//
// D3DX math functions:
//
// NOTE:
//  * All these functions can take the same object as in and out parameters.
//
//  * Out parameters are typically also returned as return values, so that
//    the output of one function may be used as a parameter to another.
//
//===========================================================================

//--------------------------
// Float16
//--------------------------

// non-inline
#ifdef __cplusplus
extern "C" {
#endif

    //// Converts an array 32-bit floats to 16-bit floats
    //D3DXFLOAT16* WINAPI D3DXFloat32To16Array
    //(D3DXFLOAT16* pOut, CONST FLOAT* pIn, UINT n);

    // Converts an array 16-bit floats to 32-bit floats
    FLOAT* WINAPI D3DXFloat16To32Array
    (FLOAT* pOut, CONST D3DXFLOAT16* pIn, UINT n);

#ifdef __cplusplus
}
#endif



//--------------------------
// 2D Vector
//--------------------------

// non-inline
#ifdef __cplusplus
extern "C" {
#endif

    D3DXVECTOR2* WINAPI D3DXVec2Normalize
    (D3DXVECTOR2* pOut, CONST D3DXVECTOR2* pV);

#ifdef __cplusplus
}
#endif

//--------------------------
// 3D Vector
//--------------------------

// inline

FLOAT D3DXVec3Length
(CONST D3DXVECTOR3* pV);

D3DXVECTOR3* D3DXVec3Scale
(D3DXVECTOR3* pOut, CONST D3DXVECTOR3* pV, FLOAT s);

// non-inline
#ifdef __cplusplus
extern "C" {
#endif

    D3DXVECTOR3* WINAPI D3DXVec3Normalize
    (D3DXVECTOR3* pOut, CONST D3DXVECTOR3* pV);


    D3DXVECTOR3* WINAPI D3DXVec3TransformNormal
    (D3DXVECTOR3* pOut, CONST D3DXVECTOR3* pV, CONST D3DXMATRIX* pM);

    // Transform (x, y, z, 1) by matrix, project result back into w=1.
    D3DXVECTOR3* WINAPI D3DXVec3TransformCoord
    (D3DXVECTOR3* pOut, CONST D3DXVECTOR3* pV, CONST D3DXMATRIX* pM);

#ifdef __cplusplus
}
#endif

//--------------------------
// 4D Vector
//--------------------------

// non-inline
#ifdef __cplusplus
extern "C" {
#endif


    // Transform vector by matrix.
    D3DXVECTOR4* WINAPI D3DXVec4Transform
    (D3DXVECTOR4* pOut, CONST D3DXVECTOR4* pV, CONST D3DXMATRIX* pM);


#ifdef __cplusplus
}
#endif

//--------------------------
// 4D Matrix
//--------------------------

// inline

D3DXMATRIX* D3DXMatrixIdentity
(D3DXMATRIX* pOut);

BOOL D3DXMatrixIsIdentity
(CONST D3DXMATRIX* pM);

// non-inline
#ifdef __cplusplus
extern "C" {
#endif


    D3DXMATRIX* WINAPI D3DXMatrixTranspose
    (D3DXMATRIX* pOut, CONST D3DXMATRIX* pM);

    // Matrix multiplication.  The result represents the transformation M2
    // followed by the transformation M1.  (Out = M1 * M2)
    D3DXMATRIX* WINAPI D3DXMatrixMultiply
    (D3DXMATRIX* pOut, CONST D3DXMATRIX* pM1, CONST D3DXMATRIX* pM2);

    D3DXMATRIX* WINAPI D3DXMatrixInverse
    (D3DXMATRIX* pOut, FLOAT* pDeterminant, CONST D3DXMATRIX* pM);

    // Build a matrix which scales by (sx, sy, sz)
    D3DXMATRIX* WINAPI D3DXMatrixScaling
    (D3DXMATRIX* pOut, FLOAT sx, FLOAT sy, FLOAT sz);

    // Build a matrix which translates by (x, y, z)
    D3DXMATRIX* WINAPI D3DXMatrixTranslation
    (D3DXMATRIX* pOut, FLOAT x, FLOAT y, FLOAT z);

    // Build a matrix which rotates around the X axis
    D3DXMATRIX* WINAPI D3DXMatrixRotationX
    (D3DXMATRIX* pOut, FLOAT Angle);

    // Build a matrix which rotates around the Y axis
    D3DXMATRIX* WINAPI D3DXMatrixRotationY
    (D3DXMATRIX* pOut, FLOAT Angle);

    // Build a matrix which rotates around the Z axis
    D3DXMATRIX* WINAPI D3DXMatrixRotationZ
    (D3DXMATRIX* pOut, FLOAT Angle);

    // Build a lookat matrix. (right-handed)
    D3DXMATRIX* WINAPI D3DXMatrixLookAtRH
    (D3DXMATRIX* pOut, CONST D3DXVECTOR3* pEye, CONST D3DXVECTOR3* pAt,
        CONST D3DXVECTOR3* pUp);

    // Build an ortho projection matrix. (right-handed)
    D3DXMATRIX* WINAPI D3DXMatrixOrthoRH
    (D3DXMATRIX* pOut, FLOAT w, FLOAT h, FLOAT zn, FLOAT zf);


    // Build a matrix which reflects the coordinate system about a plane
    D3DXMATRIX* WINAPI D3DXMatrixReflect
    (D3DXMATRIX* pOut, CONST D3DXPLANE* pPlane);

#ifdef __cplusplus
}
#endif

//--------------------------
// Plane
//--------------------------

// inline



// ax + by + cz + d
FLOAT D3DXPlaneDotCoord
(CONST D3DXPLANE* pP, CONST D3DXVECTOR3* pV);

// ax + by + cz
FLOAT D3DXPlaneDotNormal
(CONST D3DXPLANE* pP, CONST D3DXVECTOR3* pV);



// non-inline
#ifdef __cplusplus
extern "C" {
#endif

    // Normalize plane (so that |a,b,c| == 1)
    D3DXPLANE* WINAPI D3DXPlaneNormalize
    (D3DXPLANE* pOut, CONST D3DXPLANE* pP);

    // Transform a plane by a matrix.  The vector (a,b,c) must be normal.
    // M should be the inverse transpose of the transformation desired.
    D3DXPLANE* WINAPI D3DXPlaneTransform
    (D3DXPLANE* pOut, CONST D3DXPLANE* pP, CONST D3DXMATRIX* pM);
   
#ifdef __cplusplus
}
#endif


#include "d3dx9_math.inl"