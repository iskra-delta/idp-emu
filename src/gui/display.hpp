#pragma once
#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES
#endif
#include <SDL_opengl.h>
#include <cstdint>

class display
{
public:
    static constexpr int TEXT_W = 1056;
    static constexpr int TEXT_H = 312;
    static constexpr int GFX_W = 1024;
    static constexpr int GFX_H = 512;
    static constexpr int COMP_W = 1056;
    static constexpr int COMP_H = 624; // TEXT_H * 2
    static constexpr int GFX_X_OFF = (COMP_W - GFX_W) / 2;  // 16
    static constexpr int GFX_Y_OFF = (COMP_H - GFX_H) / 2;  // 56

    void init();
    void shutdown();
    void update();

    void set_text_pixel(int x, int y, bool on);
    void set_gfx_pixel(int x, int y, bool on);
    void clear_text();
    void clear_gfx();

    // Text rendering (132 cols x 24 rows, 8x13 cells)
    static constexpr int COLS = 132;
    static constexpr int ROWS = 24;
    static constexpr int CHAR_W = 8;
    static constexpr int CHAR_H = 13;
    void draw_char(int col, int row, char c);
    void draw_text(int col, int row, const char *text);

    GLuint get_texture() const { return crt_tex_; }
    static constexpr float aspect_ratio() { return (float)COMP_W / (float)COMP_H; }

private:
    uint8_t text_fb_[TEXT_W * TEXT_H]{};
    uint8_t gfx_fb_[GFX_W * GFX_H]{};
    uint8_t comp_[COMP_W * COMP_H]{};

    GLuint source_tex_ = 0;
    GLuint crt_tex_ = 0;
    GLuint fbo_ = 0;
    GLuint shader_ = 0;
    GLuint vao_ = 0;
    GLuint vbo_ = 0;

    void composite();
    void apply_crt();
    GLuint compile_shader(const char *vert_src, const char *frag_src);
};
