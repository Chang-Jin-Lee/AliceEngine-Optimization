#include "Runtime/Foundation/ThreadSafety.h"

namespace Alice
{
	namespace ThreadSafety
	{
		static std::thread::id s_mainThreadId;

		void SetMainThreadId(std::thread::id mainThreadId)
		{
			s_mainThreadId = mainThreadId;
		}

		bool IsMainThread()
		{
			return std::this_thread::get_id() == s_mainThreadId;
		}
	}
}
