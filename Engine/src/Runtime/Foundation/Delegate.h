#pragma once
#include <functional>

namespace Alice
{
	// 나중에 파라미터가 생기면 템플릿 인자만 수정하면 됩니다.
	template<typename... Args>
	class Delegate
	{
	public:
		using FunctionType = std::function<void(Args...)>;

		Delegate() = default;

		// 1. 멤버 함수 바인딩 (Unreal의 BindUObject / BindRaw)
		// 사용법: delegate.BindObject(this, &MyClass::MyFunc);
		template <typename T>
		void BindObject(T* instance, void(T::* method)(Args...))
		{
			// 람다로 래핑해서 멤버 함수 호출
			m_callback = [instance, method](Args... args)
			{
				(instance->*method)(args...);
			};
		}

		// 2. 람다 바인딩 (Unreal의 BindLambda)
		// 사용법: delegate.BindLambda([](){ ... });
		void BindLambda(FunctionType&& func)
		{
			m_callback = std::move(func);
		}

		// 3. 바인딩 해제 (Unbind)
		void Unbind()
		{
			m_callback = nullptr;
		}

		// 4. 실행 (Execute) - 바인딩 안되어 있으면 터질 수 있음
		void Execute(Args... args) const
		{
			if (IsBound())
			{
				m_callback(args...);
			}
		}

		// 바인딩 여부 확인
		bool IsBound() const { return m_callback != nullptr; }

	private:
		FunctionType m_callback;
	};



    // return값이 있는 델리게이트
    template<typename R, typename... Args>
        class DelegateRetVal
    {
    public:
        using FunctionType = std::function<R(Args...)>;

        DelegateRetVal() = default;

        // 멤버 함수 바인딩
        template <typename T>
        void BindObject(T* instance, R(T::* method)(Args...))
        {
            m_callback = [instance, method](Args... args) -> R
                {
                    return (instance->*method)(std::forward<Args>(args)...);
                };
        }

        // const 멤버 함수 바인딩도 지원
        template <typename T>
        void BindObject(T* instance, R(T::* method)(Args...) const)
        {
            m_callback = [instance, method](Args... args) -> R
                {
                    return (instance->*method)(std::forward<Args>(args)...);
                };
        }

        // 람다/함수 바인딩
        void BindLambda(FunctionType func)
        {
            m_callback = std::move(func);
        }

        void Unbind()
        {
            m_callback = nullptr;
        }

        bool IsBound() const { return static_cast<bool>(m_callback); }

        // 반환값이 있으므로, 바인딩 안 되어있으면 assert로 터지게(원하는 정책대로 변경 가능)
        R Execute(Args... args) const
        {
            assert(IsBound() && "Delegate Execute called but not bound");
            return m_callback(std::forward<Args>(args)...);
        }

        // 바인딩 안 되어있을 때 기본값 리턴하는 버전(선택)
        R ExecuteOr(R defaultValue, Args... args) const
        {
            if (!IsBound()) return defaultValue;
            return m_callback(std::forward<Args>(args)...);
        }

    private:
        FunctionType m_callback;
    };
}

// 매크로는 네임스페이스 밖에 정의해야 합니다
// 사용법: ALICE_DECLARE_DELEGATE(이름)

// 파라미터 0개
#define ALICE_DECLARE_DELEGATE(DelegateName) \
        using DelegateName = Alice::Delegate<>; 

// 파라미터 1개
#define ALICE_DECLARE_DELEGATE_OneParam(DelegateName, Param1Type) \
        using DelegateName = Alice::Delegate<Param1Type>;

// 파라미터 2개
#define ALICE_DECLARE_DELEGATE_TwoParams(DelegateName, Param1Type, Param2Type) \
        using DelegateName = Alice::Delegate<Param1Type, Param2Type>;

//  ---- 추가!! ---- return type이 있는 델리게이트
#define ALICE_DECLARE_DELEGATE_RetVal(DelegateName, RetType) \
        using DelegateName = Alice::DelegateRetVal<RetType>;

#define ALICE_DECLARE_DELEGATE_RetVal_OneParam(DelegateName, RetType, Param1Type) \
        using DelegateName = Alice::DelegateRetVal<RetType, Param1Type>;

#define ALICE_DECLARE_DELEGATE_RetVal_TwoParams(DelegateName, RetType, Param1Type, Param2Type) \
        using DelegateName = Alice::DelegateRetVal<RetType, Param1Type, Param2Type>;

// 인자 3개 + 반환값 있는 델리게이트
#define ALICE_DECLARE_DELEGATE_RetVal_ThreeParams(DelegateName, RetType, P1, P2, P3) \
        using DelegateName = Alice::DelegateRetVal<RetType, P1, P2, P3>;