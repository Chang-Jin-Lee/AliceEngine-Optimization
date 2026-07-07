#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace Alice
{
    class ResourceManager;

    /// 백그라운드 워커에서 blob(파일 IO + 복호화)을 미리 로드해
    /// ResourceManager의 캐시(LRU)에 적재한다.
    /// GPU 리소스 생성은 하지 않는다 — 밉맵 생성이 immediate context를
    /// 요구하므로 GPU 생성은 메인 스레드 몫이다.
    class AsyncBlobLoader
    {
    public:
        explicit AsyncBlobLoader(ResourceManager& rm, unsigned workerCount = 2);
        ~AsyncBlobLoader();

        AsyncBlobLoader(const AsyncBlobLoader&) = delete;
        AsyncBlobLoader& operator=(const AsyncBlobLoader&) = delete;

        /// 로드 요청 (논리 경로). 완료 순서는 요청 순서와 다를 수 있다.
        void Request(std::string logicalPath);

        /// 완료 항목 하나를 수거. 없으면 false. (메인 스레드에서 폴링)
        bool TryPopCompleted(std::string& outPath, bool& outSuccess);

        /// 아직 완료 수거되지 않은 요청 수 (진행 중 + 완료 대기)
        std::size_t PendingCount() const;

    private:
        void WorkerMain();

        ResourceManager& m_rm;
        mutable std::mutex m_mutex;
        std::condition_variable m_cv;
        std::deque<std::string> m_requests;
        std::deque<std::pair<std::string, bool>> m_completed;
        std::size_t m_inFlight = 0;
        bool m_stop = false;
        std::vector<std::thread> m_workers;
    };
}
