#pragma once
#include <algorithm>

namespace base
{
class FPS_counter
{
public:

    FPS_counter(uint32_t countdown = 60) :
        countdown_(countdown)
    {}

    bool update(float delta_time)
    {
        update_(delta_time);

        if (frame_count_ == countdown_) {
            frame_count_ = 0;
            return true;
        } else {
            return false;
        }
    }

    float frame_time() const
    {
        return frame_time_avg_;
    }

    float frame_time_min() const
    {
        return frame_time_min_;
    }

    float frame_time_max() const
    {
        return frame_time_max_;
    }

    int fps() const
    {
        return static_cast<int>(round(1000.f / frame_time_avg_));
    }

    int frame_count() const
    {
        return frame_count_;
    }

private:
    uint32_t frame_count_{0};
    uint32_t countdown_;
    float frame_time_max_{0.f};
    float frame_time_min_{0.f};
    float frame_time_avg_{0.f};

    void update_(float delta_time)
    {
        float milsec = delta_time * 1000.f;

        if (frame_count_ > 0) {
            frame_time_max_ = std::max(frame_time_max_, milsec);
            frame_time_min_ = std::min(frame_time_min_, milsec);
            frame_time_avg_ = (frame_time_avg_ * frame_count_ + milsec) / (frame_count_ + 1);
        } else {
            frame_time_max_ = milsec;
            frame_time_min_ = milsec;
            frame_time_avg_ = milsec;
        }
        frame_count_++;
    }
};
} // namespace base
