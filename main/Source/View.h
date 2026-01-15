#pragma once

#include <lvgl.h>

class View {
public:

    virtual ~View() = default;
    virtual void onStop() = 0;
};
