#pragma once

#include <functional>
#include <typeindex>

#include "../Core/Delegate.h"

// UI 시스템 전용 델리게이트 정의
using ObjectID = unsigned long;

struct CompDelegates
{
    // void* AddComponent(ObjectID id, std::type_index type, std::function<void*()> factory)
    ALICE_DECLARE_DELEGATE_RetVal_ThreeParams(
        FAddComponentDelegate,
        void*,
        ObjectID,
        std::type_index,
        std::function<void* ()>
    );
    FAddComponentDelegate AddComponent;

    // void* FindComponent(ObjectID id, std::type_index type)
    ALICE_DECLARE_DELEGATE_RetVal_TwoParams(
        FFindComponentDelegate,
        void*,
        ObjectID,
        std::type_index
    );
    FFindComponentDelegate FindComponent;

    // void RemoveComponent(ObjectID id, std::type_index type)
    ALICE_DECLARE_DELEGATE_TwoParams(
        FRemoveComponentDelegate,
        ObjectID,
        std::type_index
    );
    FRemoveComponentDelegate RemoveComponent;
};