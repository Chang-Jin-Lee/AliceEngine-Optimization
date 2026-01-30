#pragma once

// Script 전용 RTTR 등록/SerializeField 유틸
// - C++은 "변수 위에 UPROPERTY" 같은 코드 분석이 없으므로, 등록은 RTTR_REGISTRATION에서 합니다.
// - 대신 매크로로 (이름 문자열/Getter/Setter/메타데이터)를 한 번에 묶어서 짧게 씁니다.
#include <rttr/type>
#include <rttr/registration.h>
#include "Core/IScript.h"

// ---- Field declaration helpers (in .h) ----
// private 필드는 RTTR이 직접 주소를 못 잡으므로, getter/setter를 자동 생성해서 등록합니다.
//#define ALICE_SERIALIZE_FIELD(Type, Name, DefaultValue) \
//private: \
//    Type Name = DefaultValue; \
//public: \
//    Type Get_##Name() const { return Name; } \
//    void Set_##Name(Type v) { Name = v; }
//
//// ---- RTTR registration helpers (in .cpp) ----
//#define ALICE_SCRIPT_REFLECT_BEGIN(Type) \
//RTTR_REGISTRATION \
//{ \
//    rttr::registration::class_<Type>(#Type).constructor<>()
//
//// public 필드 등록 (public 멤버만)
//#define ALICE_SCRIPT_PUBLIC_FIELD(Type, Name) \
//    .property(#Name, &Type::Name)
//
//// SerializeField 등록 (ALICE_SERIALIZE_FIELD로 만든 getter/setter 대상)
//#define ALICE_SCRIPT_SERIALIZE_FIELD(Type, Name) \
//    .property(#Name, &Type::Get_##Name, &Type::Set_##Name)(rttr::metadata("SerializeField", true))
//
//// EntityId 필드에 "이 컴포넌트를 가진 엔티티만 선택" 같은 필터를 걸고 싶을 때 사용(문자열 기반)
//#define ALICE_SCRIPT_ENTITY_FIELD(Type, Name, RequiredComponentName) \
//    .property(#Name, &Type::Get_##Name, &Type::Set_##Name) \
//    (rttr::metadata("SerializeField", true), rttr::metadata("EntityRef", true), rttr::metadata("RequiredComponent", RequiredComponentName))
//
//#define ALICE_SCRIPT_REFLECT_END() \
//    ; \
//}

// ==================================================================================
// 1. 클래스 설정 매크로 (ALICE_BODY)
// ==================================================================================
#define ALICE_BODY(ClassName) \
public: \
    using ThisType = ClassName; \
    static constexpr const char* _Refl_ClassName = #ClassName; \
    RTTR_ENABLE(IScript) \
private: \
    struct ClassReflector { \
        ClassReflector() { \
            rttr::registration::class_<ClassName>(_Refl_ClassName) \
                .constructor<>(); \
        } \
    }; \
    inline static ClassReflector _reg_ctor; \
public:

// ==================================================================================
// 2. 변수(필드) 등록 매크로 (ALICE_PROPERTY)
// ==================================================================================
#define ALICE_PROPERTY(Type, Name, DefaultValue) \
private: \
    Type Name = DefaultValue; \
public: \
    Type Get_##Name() const { return Name; } \
    void Set_##Name(Type v) { Name = v; } \
private: \
    struct Reflector_##Name { \
        Reflector_##Name() { \
            rttr::registration::class_<ThisType>(ThisType::_Refl_ClassName) \
                .property(#Name, &ThisType::Get_##Name, &ThisType::Set_##Name); \
        } \
    }; \
    inline static Reflector_##Name _reg_##Name;

// ==================================================================================
// 3. 함수(메서드) 등록 매크로 (ALICE_FUNC)
// ==================================================================================
#define ALICE_FUNC(FuncName) \
private: \
    struct Reflector_Func_##FuncName { \
        Reflector_Func_##FuncName() { \
            rttr::registration::class_<ThisType>(ThisType::_Refl_ClassName) \
                .method(#FuncName, &ThisType::FuncName); \
        } \
    }; \
    inline static Reflector_Func_##FuncName _reg_func_##FuncName;

// Get, Set 자동생성 해주는 부분인데, 지금은 안쓰이긴함
#define ALICE_GET_PROP(Instance, Name) \
    (rttr::type::get(Instance).get_property(#Name).get_value(Instance))

#define ALICE_SET_PROP(Instance, Name, Value) \
    (rttr::type::get(Instance).get_property(#Name).set_value(Instance, (Value)))

#define ALICE_GET_PROP_AS(Instance, Name, Type) \
    (rttr::type::get(Instance).get_property(#Name).get_value(Instance).get_value<Type>())


