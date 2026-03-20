#pragma once
#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES
#endif
#include <SDL_opengl.h>
#include <cstdint>
#include <string>

class display
{
public:
    static constexpr int FB_W = 800;
    static constexpr int FB_H = 400;
    static constexpr int COLS = 80;
    static constexpr int ROWS = 25;
    static constexpr int CHAR_W = 10; // 5 * 2 (2x scale)
    static constexpr int CHAR_H = 16; // 7 * 2 plus a little line spacing

    void init();
    void shutdown();
    void update();

    bool load_font(const std::string &path);
    void set_pixel(int x, int y, bool on);
    void clear();

    void draw_char(int col, int row, char c);
    void draw_text(int col, int row, const char *text);
    void fill_char_cell(int col, int row);

    GLuint get_texture() const { return crt_tex_; }
    float aspect_ratio() const { return (float)FB_W / (float)FB_H; }

private:
    uint8_t fb_[FB_W * FB_H]{};
    uint8_t font_5x7_[96][7]{}; // 96 chars, 7 rows, 5 bits per row
    bool font_loaded_ = false;

    GLuint source_tex_ = 0;
    GLuint crt_tex_ = 0;
    GLuint fbo_ = 0;
    GLuint shader_ = 0;
    GLuint vao_ = 0;
    GLuint vbo_ = 0;

    void apply_crt();
    GLuint compile_shader(const char *vert_src, const char *frag_src);
};
