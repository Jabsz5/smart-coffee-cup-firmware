#pragma once

#include <atomic>
#include <stdint.h>

enum class DisplayMode : uint8_t {
    Initialization,
    SensorDashboard,
    Text,
    Photo,
    Drawing
};

extern std::atomic<DisplayMode> currentDisplayMode;