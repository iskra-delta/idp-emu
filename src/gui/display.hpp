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
        color = 3,
        flat = 4,
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
    void set_level_pixel(int x, int y, uint8_t value);
    void set_index_pixel(int x, int y, uint8_t index4);
    void clear();
    void clear_all();

    void draw_char(int col, int row, char c);
    void draw_text(int col, int row, const char *text);
    void fill_char_cell(int col, int row);

    GLuint get_texture() const { return shader_ ? crt_tex_ : source_tex_; }
    float aspect_ratio() const { return (float)FB_W / (float)FB_H; }
    const uint8_t* data() const { return fb_; }
    void set_phosphor_type(phosphor_type t)
    {
        phosphor_ = (t == phosphor_type::color) ? phosphor_type::green : t;
    }
    phosphor_type get_phosphor_type() const { return phosphor_; }
    void set_monitor_brightness(float v);
    float get_monitor_brightness() const { return monitor_brightness_; }
    void set_monitor_contrast(float v);
    float get_monitor_contrast() const { return monitor_contrast_; }
    void set_monitor_bloom(float v);
    float get_monitor_bloom() const { return monitor_bloom_; }
    void set_monitor_scanline_strength(float v);
    float get_monitor_scanline_strength() const { return monitor_scanline_strength_; }
    void set_monitor_mask_strength(float v);
    float get_monitor_mask_strength() const { return monitor_mask_strength_; }
    void set_monitor_vignette(float v);
    float get_monitor_vignette() const { return monitor_vignette_; }
    void set_monitor_persistence(float v);
    float get_monitor_persistence() const { return monitor_persistence_; }
    void reset_monitor_tuning();
    void set_text_palette_rgb(float fg_r, float fg_g, float fg_b,
                              float bg_r, float bg_g, float bg_b,
                              bool reverse_video, bool force_background);

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
    phosphor_type phosphor_ = phosphor_type::flat;
    float monitor_brightness_ = 1.00f;
    float monitor_contrast_ = 1.00f;
    float monitor_bloom_ = 1.00f;
    float monitor_scanline_strength_ = 1.00f;
    float monitor_mask_strength_ = 1.00f;
    float monitor_vignette_ = 1.00f;
    float monitor_persistence_ = 0.78f;
    float text_fg_r_ = 0.92f;
    float text_fg_g_ = 0.94f;
    float text_fg_b_ = 0.96f;
    float text_bg_r_ = 0.00f;
    float text_bg_g_ = 0.00f;
    float text_bg_b_ = 0.00f;
    bool text_reverse_video_ = false;
    bool text_force_background_ = false;

    void apply_crt();
    GLuint compile_shader(const char *vert_src, const char *frag_src);
};
