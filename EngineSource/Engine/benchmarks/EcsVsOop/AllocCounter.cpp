// 전역 operator new/delete 교체. 벤치 실행파일 전용.
// 정렬 지정 new(std::align_val_t)는 교체하지 않는다. 이 벤치의 자료구조가 쓰지 않기 때문이다.
//
// 단일 스레드 벤치이므로 g_allocCount/g_allocBytes는 atomic이 아닌 평범한 정수를 쓴다.
// std::atomic::fetch_add는 relaxed라도 x86에서 lock xadd(약 15~25 사이클)로 컴파일되는데,
// 단일 스레드에서는 lock 없는 평범한 증감(1~2 사이클)으로 충분하다.
//
// 라운드 2: 시간 측정과 메모리 측정을 같은 패스에서 재면 서로 상쇄된다는 것이 라운드 1에서
// 드러났다. atomic을 없애 할당 횟수에 비례하는 계측 비용을 지웠는데, live 바이트를 재려고
// 넣은 _msize(<malloc.h>) 호출이 할당마다 힙 메타데이터를 다시 훑어 거의 같은 크기의 비용을
// 되가져왔다 (OOP add @N=50000 실측: _msize 있으면 약 14.0ms, 없으면 12.05~12.20ms).
// 그래서 _msize 호출을 g_trackLiveBytes 플래그로 가둔다. 시간을 재는 패스에서는 이 플래그를
// 절대 켜지 않아 _msize를 한 번도 부르지 않고, allocCount/allocBytes만 평범한 정수 증가로
// 집계한다(비용 무시할 수준). live/peak 바이트는 시간을 재지 않는 별도의 메모리 전용 패스에서만
// 이 플래그를 켜고 측정한다 - Main.cpp의 MeasurePeakLiveBytes() 참고.
#include <cstdint>
#include <cstdlib>
#include <malloc.h>
#include <new>

namespace Alice::Bench
{
    std::uint64_t g_allocCount = 0;     // 누적 할당 횟수 (모든 패스에서 집계. 비용이 작아 그대로 둔다)
    std::uint64_t g_allocBytes = 0;     // 누적 요청 바이트 (churn 지표. 실사용량이 아니다)
    std::int64_t  g_liveBytes = 0;      // 현재 점유 바이트 (_msize 기준). g_trackLiveBytes가 true일 때만 갱신된다.
    std::int64_t  g_peakLiveBytes = 0;  // 메모리 전용 패스 구간 내 최고 점유 바이트
    std::int64_t  g_minLiveBytes = 0;   // 진단용 워터마크. g_liveBytes가 도달한 최저값 (음수면 new/delete 회계 오류).
    bool          g_trackLiveBytes = false; // 메모리 전용 패스에서만 true. 시간 패스에서는 절대 켜지 않는다.
}

void* operator new(std::size_t size)
{
    void* p = std::malloc(size);
    if (p == nullptr)
        throw std::bad_alloc();

    Alice::Bench::g_allocCount += 1;
    Alice::Bench::g_allocBytes += size;
    if (Alice::Bench::g_trackLiveBytes)
    {
        Alice::Bench::g_liveBytes += static_cast<std::int64_t>(_msize(p));
        if (Alice::Bench::g_liveBytes > Alice::Bench::g_peakLiveBytes)
            Alice::Bench::g_peakLiveBytes = Alice::Bench::g_liveBytes;
    }
    return p;
}

void* operator new[](std::size_t size) { return operator new(size); }

void operator delete(void* p) noexcept
{
    if (p == nullptr)
        return;
    if (Alice::Bench::g_trackLiveBytes)
    {
        Alice::Bench::g_liveBytes -= static_cast<std::int64_t>(_msize(p));
        if (Alice::Bench::g_liveBytes < Alice::Bench::g_minLiveBytes)
            Alice::Bench::g_minLiveBytes = Alice::Bench::g_liveBytes;
    }
    std::free(p);
}

void operator delete[](void* p) noexcept { operator delete(p); }
void operator delete(void* p, std::size_t) noexcept { operator delete(p); }
void operator delete[](void* p, std::size_t) noexcept { operator delete(p); }
