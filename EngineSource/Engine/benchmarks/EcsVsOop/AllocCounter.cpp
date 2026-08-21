// 전역 operator new/delete 교체. 벤치 실행파일 전용.
// 정렬 지정 new(std::align_val_t)는 교체하지 않는다. 이 벤치의 자료구조가 쓰지 않기 때문이다.
//
// 이 벤치는 단일 스레드다. 그래서 atomic이 아닌 평범한 정수를 쓴다.
// std::atomic::fetch_add는 relaxed라도 x86에서 lock xadd(약 15~25 사이클, 스토어 버퍼 드레인)로
// 컴파일되고, 할당마다 2회(count/bytes) 발생한다. 계측 비용이 할당 횟수에 비례하는 셈인데,
// 이 벤치의 두 백엔드는 할당 횟수가 최대 1600배 차이 나므로 계측 비용이 정확히 그 축(백엔드 정체성)에
// 실려 결과를 왜곡한다. 단일 스레드에서는 lock 없는 평범한 증감(1~2 사이클)으로 충분하다.
//
// _msize(<malloc.h>)로 malloc이 실제로 내준 블록 크기를 재어 live/peak 점유 바이트를 추적한다.
// operator new의 size 인자(요청 바이트, g_allocBytes)와 _msize가 돌려주는 실제 점유 바이트는
// 서로 다른 지표다 - 전자는 힙 granularity와 delete 시 회수분을 반영하지 못하는 누적 요청량(churn)이고,
// 후자만이 특정 시점의 실제 메모리 점유량을 말해준다. 그래서 둘 다 남긴다.
#include <cstdint>
#include <cstdlib>
#include <malloc.h>
#include <new>

namespace Alice::Bench
{
    std::uint64_t g_allocCount = 0;    // 누적 할당 횟수
    std::uint64_t g_allocBytes = 0;    // 누적 요청 바이트 (할당자 churn 지표. 실사용량이 아니다)
    std::int64_t  g_liveBytes = 0;     // 현재 점유 바이트 (_msize 기준). 음수로 가면 new/delete 회계 오류다.
    std::int64_t  g_peakLiveBytes = 0; // 마지막 ResetAllocStats() 이후 구간 내 최고 점유 바이트
    std::int64_t  g_liveAtReset = 0;   // 마지막 ResetAllocStats() 호출 시점의 g_liveBytes 스냅샷
    std::int64_t  g_minLiveBytes = 0;  // 프로세스 시작 이후 g_liveBytes가 도달한 최저값. 음수면 new/delete 회계가 깨졌다는 뜻이다.
}

void* operator new(std::size_t size)
{
    void* p = std::malloc(size);
    if (p == nullptr)
        throw std::bad_alloc();

    Alice::Bench::g_allocCount += 1;
    Alice::Bench::g_allocBytes += size;
    Alice::Bench::g_liveBytes += static_cast<std::int64_t>(_msize(p));
    if (Alice::Bench::g_liveBytes > Alice::Bench::g_peakLiveBytes)
        Alice::Bench::g_peakLiveBytes = Alice::Bench::g_liveBytes;
    return p;
}

void* operator new[](std::size_t size) { return operator new(size); }

void operator delete(void* p) noexcept
{
    if (p == nullptr)
        return;
    Alice::Bench::g_liveBytes -= static_cast<std::int64_t>(_msize(p));
    if (Alice::Bench::g_liveBytes < Alice::Bench::g_minLiveBytes)
        Alice::Bench::g_minLiveBytes = Alice::Bench::g_liveBytes;
    std::free(p);
}

void operator delete[](void* p) noexcept { operator delete(p); }
void operator delete(void* p, std::size_t) noexcept { operator delete(p); }
void operator delete[](void* p, std::size_t) noexcept { operator delete(p); }
