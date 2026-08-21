// 전역 operator new/delete 교체. 벤치 실행파일 전용.
// 정렬 지정 new(std::align_val_t)는 교체하지 않는다. 이 벤치의 자료구조가 쓰지 않기 때문이다.

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <new>

namespace Alice::Bench
{
    std::atomic<std::uint64_t> g_allocCount{ 0 };
    std::atomic<std::uint64_t> g_allocBytes{ 0 };
}

void* operator new(std::size_t size)
{
    Alice::Bench::g_allocCount.fetch_add(1, std::memory_order_relaxed);
    Alice::Bench::g_allocBytes.fetch_add(size, std::memory_order_relaxed);
    void* p = std::malloc(size);
    if (p == nullptr)
        throw std::bad_alloc();
    return p;
}

void* operator new[](std::size_t size) { return operator new(size); }

void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
