#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

// 벤치 시간 표본 통계. 순수 함수만 두어 테스트로 고정한다.
namespace Alice::BenchStats
{
    /// 중앙값. 표본을 값으로 받아 내부에서 정렬하므로 호출자의 순서는 보존된다.
    inline double Median(std::vector<double> samples)
    {
        if (samples.empty())
            return 0.0;

        std::sort(samples.begin(), samples.end());
        const std::size_t mid = samples.size() / 2;
        if (samples.size() % 2 == 1)
            return samples[mid];
        return (samples[mid - 1] + samples[mid]) * 0.5;
    }

    /// 표본 표준편차(n-1). 표본이 2개 미만이면 0.
    inline double StdDev(const std::vector<double>& samples)
    {
        if (samples.size() < 2)
            return 0.0;

        double sum = 0.0;
        for (const double v : samples)
            sum += v;
        const double mean = sum / static_cast<double>(samples.size());

        double acc = 0.0;
        for (const double v : samples)
        {
            const double d = v - mean;
            acc += d * d;
        }
        return std::sqrt(acc / static_cast<double>(samples.size() - 1));
    }
}
