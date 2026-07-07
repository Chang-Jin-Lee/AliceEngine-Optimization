#include "Runtime/Resources/AsyncBlobLoader.h"
#include "Runtime/Resources/ResourceManager.h"
#include "Runtime/Foundation/Logger.h"

namespace Alice
{
    AsyncBlobLoader::AsyncBlobLoader(ResourceManager& rm, unsigned workerCount)
        : m_rm(rm)
    {
        if (workerCount == 0) workerCount = 1;
        m_workers.reserve(workerCount);
        for (unsigned i = 0; i < workerCount; ++i)
            m_workers.emplace_back([this] { WorkerMain(); });
    }

    AsyncBlobLoader::~AsyncBlobLoader()
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_stop = true;
        }
        m_cv.notify_all();
        for (auto& t : m_workers)
            if (t.joinable()) t.join();
    }

    void AsyncBlobLoader::Request(std::string logicalPath)
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_requests.push_back(std::move(logicalPath));
        }
        m_cv.notify_one();
    }

    bool AsyncBlobLoader::TryPopCompleted(std::string& outPath, bool& outSuccess)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_completed.empty())
            return false;
        outPath = std::move(m_completed.front().first);
        outSuccess = m_completed.front().second;
        m_completed.pop_front();
        return true;
    }

    std::size_t AsyncBlobLoader::PendingCount() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_requests.size() + m_inFlight + m_completed.size();
    }

    void AsyncBlobLoader::WorkerMain()
    {
        for (;;)
        {
            std::string path;
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_cv.wait(lock, [this] { return m_stop || !m_requests.empty(); });
                if (m_stop)
                    return;
                path = std::move(m_requests.front());
                m_requests.pop_front();
                ++m_inFlight;
            }

            const bool ok = (m_rm.LoadSharedBinaryAuto(path) != nullptr);

            {
                std::lock_guard<std::mutex> lock(m_mutex);
                --m_inFlight;
                m_completed.emplace_back(std::move(path), ok);
            }
        }
    }
}
