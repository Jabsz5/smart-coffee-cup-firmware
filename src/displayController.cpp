#include "displayController.h"

std::atomic<DisplayMode> currentDisplayMode{
    DisplayMode::Initialization
};