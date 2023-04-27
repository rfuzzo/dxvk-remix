
#include "funcinput.h"
#ifdef MGE_INPUT
#include "mge/mgedinput.h"
#endif // MGE_INPUT


MWSEINSTRUCTION_DECLARE_VTABLE(mwseTapKey)

// TapKey
bool mwseTapKey::execute(mwseInstruction* _this) {
    VMREGTYPE key;
    if (!_this->vmPop(&key)) { return false; }

#ifdef MGE_INPUT
    MGEProxyDirectInput::changeKeyBehavior(key, MGEProxyDirectInput::TAP, true);
#endif // MGE_INPUT
    return true;
}

MWSEINSTRUCTION_DECLARE_VTABLE(mwsePushKey)

// PushKey
bool mwsePushKey::execute(mwseInstruction* _this) {
    VMREGTYPE key;
    if (!_this->vmPop(&key)) { return false; }

#ifdef MGE_INPUT    
    MGEProxyDirectInput::changeKeyBehavior(key, MGEProxyDirectInput::PUSH, true);
#endif // MGE_INPUT
    return true;
}

MWSEINSTRUCTION_DECLARE_VTABLE(mwseReleaseKey)

// ReleaseKey
bool mwseReleaseKey::execute(mwseInstruction* _this) {
    VMREGTYPE key;
    if (!_this->vmPop(&key)) { return false; }

#ifdef MGE_INPUT    
    MGEProxyDirectInput::changeKeyBehavior(key, MGEProxyDirectInput::PUSH, false);
#endif // MGE_INPUT
    return true;
}

MWSEINSTRUCTION_DECLARE_VTABLE(mwseHammerKey)

// HammerKey
bool mwseHammerKey::execute(mwseInstruction* _this) {
    VMREGTYPE key;
    if (!_this->vmPop(&key)) { return false; }

#ifdef MGE_INPUT
    MGEProxyDirectInput::changeKeyBehavior(key, MGEProxyDirectInput::HAMMER, true);
#endif // MGE_INPUT
    return true;
}

MWSEINSTRUCTION_DECLARE_VTABLE(mwseUnhammerKey)

// UnhammerKey
bool mwseUnhammerKey::execute(mwseInstruction* _this) {
    VMREGTYPE key;
    if (!_this->vmPop(&key)) { return false; }

#ifdef MGE_INPUT
    MGEProxyDirectInput::changeKeyBehavior(key, MGEProxyDirectInput::HAMMER, false);
#endif // MGE_INPUT
    return true;
}

MWSEINSTRUCTION_DECLARE_VTABLE(mwseAHammerKey)

// AHammerKey
bool mwseAHammerKey::execute(mwseInstruction* _this) {
    VMREGTYPE key;
    if (!_this->vmPop(&key)) { return false; }

#ifdef MGE_INPUT
    MGEProxyDirectInput::changeKeyBehavior(key, MGEProxyDirectInput::AHAMMER, true);
#endif // MGE_INPUT
    return true;
}

MWSEINSTRUCTION_DECLARE_VTABLE(mwseAUnhammerKey)

// AUnhammerKey
bool mwseAUnhammerKey::execute(mwseInstruction* _this) {
    VMREGTYPE key;
    if (!_this->vmPop(&key)) { return false; }

#ifdef MGE_INPUT
    MGEProxyDirectInput::changeKeyBehavior(key, MGEProxyDirectInput::AHAMMER, false);
#endif // MGE_INPUT
    return true;
}

MWSEINSTRUCTION_DECLARE_VTABLE(mwseDisallowKey)

// DisallowKey
bool mwseDisallowKey::execute(mwseInstruction* _this) {
    VMREGTYPE key;
    if (!_this->vmPop(&key)) { return false; }

#ifdef MGE_INPUT
    MGEProxyDirectInput::changeKeyBehavior(key, MGEProxyDirectInput::DISALLOW, true);
#endif // MGE_INPUT
    return true;
}

MWSEINSTRUCTION_DECLARE_VTABLE(mwseAllowKey)

// AllowKey
bool mwseAllowKey::execute(mwseInstruction* _this) {
    VMREGTYPE key;
    if (!_this->vmPop(&key)) { return false; }

#ifdef MGE_INPUT
    MGEProxyDirectInput::changeKeyBehavior(key, MGEProxyDirectInput::DISALLOW, false);
#endif // MGE_INPUT
    return true;
}
