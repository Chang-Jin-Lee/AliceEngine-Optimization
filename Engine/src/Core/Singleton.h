#pragma once
#include <cassert>

/*
* @brief Singleton 클래스.
* @details 이 클래스를 상속받은 클래스는 싱글톤 패턴을 적용할 수 있습니다.
*/

//template <typename T>
//class Singleton {
//public:
//	Singleton()
//	{
//		assert(s_instance == nullptr && "Singleton instance already created!");
//		s_instance = static_cast<T*>(this);
//	}
//
//	virtual ~Singleton() = default;
//
//	static void Create()
//	{
//		if (!s_instance)
//			s_instance = new T();
//	}
//	// 명시적인 인스턴스 파괴
//	static void Destroy()
//	{
//		delete s_instance;
//		s_instance = nullptr;
//	}
//
//	// 복사 및 이동 금지
//	Singleton(const Singleton&) = delete;
//	Singleton& operator=(const Singleton&) = delete;
//	Singleton(Singleton&&) = delete;
//	Singleton& operator=(Singleton&&) = delete;
//
//	static T& GetInstance()
//	{
//		assert(s_instance != nullptr && "Singleton instance not created!");
//		return *s_instance;
//	}
//private:
//	static T* s_instance;
//};
//
//template <typename T>
//T* Singleton<T>::s_instance = nullptr;
//
//#define GetSingleton(type) Singleton<type>::Get()


template <typename T>
class Singleton
{
public:
	// 헤더에서 초기화
	static inline T* s_instance = nullptr;

	Singleton() = default;
	virtual ~Singleton() = default;

	// 복사 및 이동 금지
	Singleton(const Singleton&) = delete;
	Singleton& operator=(const Singleton&) = delete;

	// [생성] T의 생성자 파라미터 전달 가능
	static void Create()
	{
		if (!s_instance)
			s_instance = new T();
	}

	// [파괴]
	static void Destroy()
	{
		if (s_instance)
		{
			delete s_instance;
			s_instance = nullptr;
		}
	}

	// [접근]
	static T& Get()
	{
		assert(s_instance != nullptr && "Singleton not created! Call Create() first.");
		return *s_instance;
	}

	// 포인터 접근이 필요할 경우
	static T* GetPtr()
	{
		return s_instance;
	}
};

// 매크로 유지
#define GetSingleton(type) Singleton<type>::Get()