#pragma once

class display;

namespace panels {
    struct display_viewport_info {
        float x0 = 0.0f;
        float y0 = 0.0f;
        float x1 = 0.0f;
        float y1 = 0.0f;
        bool hovered = false;
    };

    void render_display(display &disp, display_viewport_info *out_info = nullptr);
}
