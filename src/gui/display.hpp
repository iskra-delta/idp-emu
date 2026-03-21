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
    enum class phosphor_type : uint8_t {
        green = 0,
        orange = 1,
        lcd = 2,
    };

    // Partner GDP visible text raster:
    // 132x26 chars at 8x12 => 1056x624.
    static constexpr int FB_W = 1056;
    static constexpr int FB_H = 624;
    static constexpr int COLS = 80;
    static constexpr int ROWS = 25;
    static constexpr int CHAR_W = 10; // 5 * 2 (2x scale)
    static constexpr int CHAR_H = 16; // 7 * 2 plus a little line spacing

    void init();
    void shutdown();
    void update();

    bool load_font(const std::string &path);
    void set_pixel(int x, int y, bool on);
    void add_pixel(int x, int y, uint8_t value);
    void clear();
    void clear_all();

    void draw_char(int col, int row, char c);
    void draw_text(int col, int row, const char *text);
    void fill_char_cell(int col, int row);

    GLuint get_texture() const { return shader_ ? crt_tex_ : source_tex_; }
    float aspect_ratio() const { return (float)FB_W / (float)FB_H; }
    const uint8_t* data() const { return fb_; }
    void set_phosphor_type(phosphor_type t) { phosphor_ = t; }
    phosphor_type get_phosphor_type() const { return phosphor_; }

private:
    uint8_t fb_[FB_W * FB_H]{};
    uint8_t ghost_fb_[FB_W * FB_H]{};
    uint8_t font_5x7_[96][7]{}; // 96 chars, 7 rows, 5 bits per row
    bool font_loaded_ = false;

    GLuint source_tex_ = 0;
    GLuint crt_tex_ = 0;
    GLuint fbo_ = 0;
    GLuint shader_ = 0;
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    phosphor_type phosphor_ = phosphor_type::green;

    void apply_crt();
    GLuint compile_shader(const char *vert_src, const char *frag_src);
};
