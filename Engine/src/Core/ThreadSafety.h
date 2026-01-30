#pragma once

#include <thread>
#include <cassert>

namespace Alice
{
	/// 메인 스레드 ID를 저장하고 검증하는 유틸리티
	namespace ThreadSafety
	{
		/// 메인 스레드 ID를 설정합니다 (Engine::Initialize에서 호출)
		void SetMainThreadId(std::thread::id mainThreadId);

		/// 현재 스레드가 메인 스레드인지 확인합니다
		bool IsMainThread();

		/// 메인 스레드가 아니면 assert를 발생시킵니다
		inline void AssertMainThread()
		{
			assert(IsMainThread() && "This function must be called from the main thread!");
		}
	}
}
