// GameTimer.cpp (AliceRenderer용) - Frank Luna 코드 기반

#include <Windows.h>
#include "Runtime/Engine/TimeSystem.h"

namespace Alice
{
    GameTimer* GameTimer::m_Instance = nullptr;

    GameTimer::GameTimer()
        : mSecondsPerCount(0.0)
        , mDeltaTime(-1.0)
        , mBaseTime(0)
        , mPausedTime(0)
        , mStopTime(0)
        , mPrevTime(0)
        , mCurrTime(0)
        , mStopped(false)
    {
        __int64 countsPerSec;
        QueryPerformanceFrequency(reinterpret_cast<LARGE_INTEGER*>(&countsPerSec));
        mSecondsPerCount = 1.0 / static_cast<double>(countsPerSec);

        if (m_Instance == nullptr) m_Instance = this;
    }

    float GameTimer::TotalTime() const
    {
        if (mStopped)
        {
            return static_cast<float>(((mStopTime - mPausedTime) - mBaseTime) * mSecondsPerCount);
        }
        else
        {
            return static_cast<float>(((mCurrTime - mPausedTime) - mBaseTime) * mSecondsPerCount);
        }
    }

    float GameTimer::DeltaTime() const
    {
        return static_cast<float>(mDeltaTime);
    }

    void GameTimer::Reset()
    {
        __int64 currTime;
        QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER*>(&currTime));

        mBaseTime = currTime;
        mPrevTime = currTime;
        mStopTime = 0;
        mStopped  = false;
    }

    void GameTimer::Start()
    {
        __int64 startTime;
        QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER*>(&startTime));

        if (mStopped)
        {
            mPausedTime += (startTime - mStopTime);

            mPrevTime = startTime;
            mStopTime = 0;
            mStopped  = false;
        }
    }

    void GameTimer::Stop()
    {
        if (!mStopped)
        {
            __int64 currTime;
            QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER*>(&currTime));

            mStopTime = currTime;
            mStopped  = true;
        }
    }

    void GameTimer::Tick()
    {
        if (mStopped)
        {
            mDeltaTime = 0.0;
            return;
        }

        __int64 currTime;
        QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER*>(&currTime));
        mCurrTime = currTime;

        // Time difference between this frame and the previous.
        mDeltaTime = (mCurrTime - mPrevTime) * mSecondsPerCount;

        // Prepare for next frame.
        mPrevTime = mCurrTime;

        // Force nonnegative.
        if (mDeltaTime < 0.0) mDeltaTime = 0.0;
    }
}



