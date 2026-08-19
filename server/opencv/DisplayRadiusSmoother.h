#pragma once

#include <algorithm>
#include <cstddef>
#include <deque>
#include <vector>

// 화재 박스 폭이 한두 프레임 흔들릴 때 전광판 반경이 매번 바뀌지 않도록
// 최근 반경의 중앙값을 구하고, 같은 변경 후보가 연속으로 확인된 뒤 적용한다.
class DisplayRadiusSmoother
{
public:
    int update(int rawRadiusCells)
    {
        if (rawRadiusCells <= 0) return stableRadiusCells_;

        samples_.push_back(rawRadiusCells);
        while (samples_.size() > SAMPLE_LIMIT) samples_.pop_front();

        std::vector<int> sorted(samples_.begin(), samples_.end());
        std::sort(sorted.begin(), sorted.end());
        const int median = sorted[sorted.size() / 2];

        if (stableRadiusCells_ <= 0)
        {
            stableRadiusCells_ = median;
            clearPending();
        }
        else if (median == stableRadiusCells_)
        {
            clearPending();
        }
        else
        {
            if (pendingRadiusCells_ == median)
                ++pendingHits_;
            else
            {
                pendingRadiusCells_ = median;
                pendingHits_ = 1;
            }

            if (pendingHits_ >= CHANGE_CONFIRM_RESULTS)
            {
                stableRadiusCells_ = median;
                clearPending();
            }
        }
        return stableRadiusCells_;
    }

    void reset()
    {
        samples_.clear();
        stableRadiusCells_ = 0;
        clearPending();
    }

private:
    static constexpr std::size_t SAMPLE_LIMIT = 5;
    static constexpr int CHANGE_CONFIRM_RESULTS = 3;

    void clearPending()
    {
        pendingRadiusCells_ = 0;
        pendingHits_ = 0;
    }

    std::deque<int> samples_;
    int stableRadiusCells_ = 0;
    int pendingRadiusCells_ = 0;
    int pendingHits_ = 0;
};
