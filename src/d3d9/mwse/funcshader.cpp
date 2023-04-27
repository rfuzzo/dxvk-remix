
#include "funcshader.h"
#ifdef MGE_SHADERS
#include "mge/postshaders.h"
#endif // MGE_SHADERS
#include <string>



MWSEINSTRUCTION_DECLARE_VTABLE(mwseSetShaderLong)

bool mwseSetShaderLong::execute(mwseInstruction* _this) {
    const char* shader = _this->vmPopString();
    if (!shader) { return false; }

    const char* varName = _this->vmPopString();
    if (!varName) { return false; }

    VMREGTYPE x;
    if (!_this->vmPop(&x)) { return false; }
#ifdef MGE_SHADERS
    PostShaders::setShaderVar(shader, varName, (int)x);
#endif // MGE_SHADERS
    return true;
}


MWSEINSTRUCTION_DECLARE_VTABLE(mwseSetShaderFloat)

bool mwseSetShaderFloat::execute(mwseInstruction* _this) {
    const char* shader = _this->vmPopString();
    if (!shader) { return false; }

    const char* varName = _this->vmPopString();
    if (!varName) { return false; }

    VMFLOAT x;
    if (!_this->vmPop(&x)) { return false; }
#ifdef MGE_SHADERS
    PostShaders::setShaderVar(shader, varName, x);
#endif // MGE_SHADERS
    return true;
}


MWSEINSTRUCTION_DECLARE_VTABLE(mwseSetShaderVector)

bool mwseSetShaderVector::execute(mwseInstruction* _this) {
    const char* shader = _this->vmPopString();
    if (!shader) { return false; }

    const char* varName = _this->vmPopString();
    if (!varName) { return false; }

    VMFLOAT v[4];
    if (!_this->vmPop(&v[0])) { return false; }
    if (!_this->vmPop(&v[1])) { return false; }
    if (!_this->vmPop(&v[2])) { return false; }
    if (!_this->vmPop(&v[3])) { return false; }
#ifdef MGE_SHADERS
    PostShaders::setShaderVar(shader, varName, v);
#endif // MGE_SHADERS
    return true;
}


MWSEINSTRUCTION_DECLARE_VTABLE(mwseEnableShader)

bool mwseEnableShader::execute(mwseInstruction* _this) {
    const char* shader = _this->vmPopString();
    if (!shader) { return false; }
#ifdef MGE_SHADERS
    PostShaders::setShaderEnable(shader, true);
#endif // MGE_SHADERS
    return true;
}


MWSEINSTRUCTION_DECLARE_VTABLE(mwseDisableShader)

bool mwseDisableShader::execute(mwseInstruction* _this) {
    const char* shader = _this->vmPopString();
    if (!shader) { return false; }
#ifdef MGE_SHADERS
    PostShaders::setShaderEnable(shader, false);
#endif // MGE_SHADERS
    return true;
}
