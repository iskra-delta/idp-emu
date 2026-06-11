#include "display.hpp"
#include "ef9367_font.hpp"
#include <SDL.h>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <iostream>

static inline float clampf(float v, float lo, float hi)
{
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

static const char *crt_vert_src = R"(
#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aTexCoord;
out vec2 TexCoord;
void main() {
    gl_Position = vec4(aPos, 0.0, 1.0);
    TexCoord = aTexCoord;
}
)";

static const char *crt_frag_src = R"(
#version 330 core
in vec2 TexCoord;
out vec4 FragColor;

uniform sampler2D screen_texture;
uniform vec2 resolution;
uniform int monitor_mode;      // 0=green CRT, 1=orange CRT, 2=LCD, 3=color CRT
uniform vec3 phosphor_core;
uniform vec3 phosphor_glow;
uniform vec3 glass_ambient;
uniform vec3 text_fg;
uniform vec3 text_bg;
uniform float text_reverse;
uniform float text_force_background;
uniform float time_sec;
uniform float monitor_brightness;
uniform float monitor_contrast;
uniform float monitor_bloom;
uniform float monitor_scanline;
uniform float monitor_mask;
uniform float monitor_vignette;

float hash21(vec2 p) {
    p = fract(p * vec2(123.34, 345.45));
    p += dot(p, p + 34.345);
    return fract(p.x * p.y);
}

vec3 decode_index_rgb(int code) {
    int idx = code & 0x0F;
    int r = (idx >> 2) & 1;
    int g = (idx >> 1) & 1;
    int b = idx & 1;
    int i = (idx >> 3) & 1;
    float hi = (i != 0) ? 1.00 : 0.72;
    return vec3(
        (r != 0) ? hi : 0.0,
        (g != 0) ? hi : 0.0,
        (b != 0) ? hi : 0.0);
}

float decode_mono_index_signal(float sample_r) {
    return sample_r;
}

float source_sample(vec2 uv) {
    // For green/orange CRT we need smooth resampling after barrel warp, otherwise
    // nearest-neighbor pickup can drop thin glyph strokes at certain angles.
    if (monitor_mode == 0 || monitor_mode == 1) {
        vec2 texel = 1.0 / resolution;
        vec2 st = uv * resolution - vec2(0.5);
        vec2 cell = floor(st);
        vec2 w = fract(st);
        // Smooth interpolation weights reduce staircase artifacts.
        w = w * w * (3.0 - 2.0 * w);
        vec2 uv00 = (cell + vec2(0.5, 0.5)) * texel;
        vec2 uv10 = uv00 + vec2(texel.x, 0.0);
        vec2 uv01 = uv00 + vec2(0.0, texel.y);
        vec2 uv11 = uv00 + texel;
        float s00 = texture(screen_texture, uv00).r;
        float s10 = texture(screen_texture, uv10).r;
        float s01 = texture(screen_texture, uv01).r;
        float s11 = texture(screen_texture, uv11).r;
        return mix(mix(s00, s10, w.x), mix(s01, s11, w.x), w.y);
    }
    return texture(screen_texture, uv).r;
}

float crt_sample(vec2 uv) {
    float s = source_sample(uv);
    if (monitor_mode == 0 || monitor_mode == 1) {
        return decode_mono_index_signal(s);
    }
    return s;
}

void main() {
    vec2 uv = TexCoord;
    vec2 texel = 1.0 / resolution;
    float edge_mask = 1.0;

    // Flat path: direct framebuffer presentation. No curvature, scanlines,
    // bloom, mask, vignette or noise — maximum legibility.
    if (monitor_mode == 4) {
        float code_f = floor(texture(screen_texture, uv).r * 255.0 + 0.5);
        int code = int(clamp(code_f, 0.0, 255.0));
        vec3 color;
        if (code >= 240) {
            color = decode_index_rgb(code);
        } else {
            color = phosphor_core * (code_f / 255.0);
        }
        color = (color - vec3(0.5)) * monitor_contrast + vec3(0.5);
        color *= monitor_brightness;
        FragColor = vec4(clamp(color, vec3(0.0), vec3(1.0)), 1.0);
        return;
    }

    // CRT path (green/orange): analog look, tuned to keep text legible.
    if (monitor_mode != 2) {
        // Mild underscan + subtle barrel distortion.
        uv = (uv - 0.5) * 1.02 + 0.5;

        vec2 cc = uv - 0.5;
        float r2 = dot(cc, cc);
        uv = cc * (1.0 + 0.035 * r2 + 0.055 * r2 * r2) + 0.5;

        // Rounded corners / bezel cut with a soft transition to avoid dropped edge pixels.
        float edge = min(min(uv.x, uv.y), min(1.0 - uv.x, 1.0 - uv.y));
        edge_mask = smoothstep(-0.006, 0.004, edge);
        uv = clamp(uv, 0.0, 1.0);
    }

    float pixel_code = source_sample(uv);
    float pixel_raw = (monitor_mode == 0 || monitor_mode == 1)
        ? decode_mono_index_signal(pixel_code)
        : pixel_code;
    float pixel = pixel_raw;

    // Post-warp antialiasing for CRT path: recover detail lost by curvature
    // sampling. Center-weighted so glyph strokes stay sharp.
    if (monitor_mode != 2) {
        float aa = 0.52 * pixel_raw;
        aa += 0.14 * crt_sample(uv + vec2(texel.x, 0.0));
        aa += 0.14 * crt_sample(uv - vec2(texel.x, 0.0));
        aa += 0.09 * crt_sample(uv + vec2(0.0, texel.y));
        aa += 0.09 * crt_sample(uv - vec2(0.0, texel.y));
        aa += 0.01 * crt_sample(uv + texel);
        aa += 0.01 * crt_sample(uv - texel);
        aa = clamp(aa, 0.0, 1.0);
        if (monitor_mode == 0 || monitor_mode == 1) {
            // Preserve dark text strokes against bright inverse blocks.
            float preserve = smoothstep(0.0, 0.12, pixel_raw);
            pixel = mix(pixel_raw, aa, preserve);
        } else {
            pixel = aa;
        }
    }
    if (monitor_mode == 0 || monitor_mode == 1) {
        // Slightly darken low-level phosphor drive so inverse backgrounds
        // don't glow, without crushing standard-intensity text.
        pixel = pow(clamp(pixel, 0.0, 1.0), 1.15);
    }

    // LCD path (Game Boy-like): reflective panel with dark ink on bright background.
    if (monitor_mode == 2) {
        float ink = 0.68 * pixel
                  + 0.22 * texture(screen_texture, uv - vec2(texel.x * 1.2, 0.0)).r
                  + 0.10 * texture(screen_texture, uv - vec2(0.0, texel.y * 0.7)).r;
        float q = clamp(floor(ink * 4.0 + 0.001), 0.0, 3.0);
        int level = int(q);

        vec3 paper_top = vec3(0.93, 0.97, 0.80);
        vec3 paper_bot = vec3(0.82, 0.90, 0.67);
        vec3 paper = mix(paper_top, paper_bot, clamp(uv.y * 1.05, 0.0, 1.0));

        // Four classic DMG-like green tones (bright -> dark).
        vec3 pal0 = paper;
        vec3 pal1 = vec3(0.67, 0.80, 0.49);
        vec3 pal2 = vec3(0.39, 0.54, 0.26);
        vec3 pal3 = vec3(0.15, 0.24, 0.11);
        vec3 color = (level == 0) ? pal0
                   : (level == 1) ? pal1
                   : (level == 2) ? pal2
                                  : pal3;

        // Reflective sheen and panel texture.
        float sheen = pow(1.0 - abs(uv.y * 2.0 - 1.0), 3.5) * 0.05;
        float grain = (hash21(uv * resolution * 0.35 + time_sec * 0.03) - 0.5) * 0.02;
        color += vec3(0.04, 0.05, 0.03) * sheen + vec3(grain);

        // Pixel matrix + inter-cell gaps.
        vec2 pix = fract(uv * resolution);
        float gap_x = smoothstep(0.88, 0.995, pix.x);
        float gap_y = smoothstep(0.86, 0.995, pix.y);
        float grid = 1.0 - 0.10 * gap_x - 0.13 * gap_y;
        color *= grid;

        // Slight edge darkening.
        vec2 vig = uv * (1.0 - uv);
        color *= clamp(pow(vig.x * vig.y * 20.0, 0.12), 0.86, 1.0);

        color = (color - vec3(0.5)) * monitor_contrast + vec3(0.5);
        color *= monitor_brightness;
        color *= edge_mask;
        color = clamp(color, vec3(0.0), vec3(1.0));
        FragColor = vec4(color, 1.0);
        return;
    }

    // Color CRT path: board-driven text color/mono controls, then analog CRT pass.
    if (monitor_mode == 3) {
        float px_code_f = floor(pixel_code * 255.0 + 0.5);
        int px_code = int(clamp(px_code_f, 0.0, 255.0));
        vec3 color;

        if (px_code >= 240) {
            // Indexed color path (0xF0..0xFF): low nibble = RGBI palette index.
            color = decode_index_rgb(px_code);
        } else {
            // Grayscale fallback path (GDP graphics plane / legacy text paths).
            float p = pixel;
            if (text_reverse > 0.5) {
                p = 1.0 - p;
            }
            vec3 fg = text_fg;
            vec3 bg = text_bg;
            if (text_force_background > 0.5) {
                bg = max(bg, vec3(0.035, 0.035, 0.070));
            }
            color = mix(bg, fg, clamp(p, 0.0, 1.0));
        }

        float glow = 0.0;
        glow += texture(screen_texture, uv).r * 0.28;
        glow += texture(screen_texture, uv + vec2(texel.x * 1.0, 0.0)).r * 0.17;
        glow += texture(screen_texture, uv - vec2(texel.x * 1.0, 0.0)).r * 0.17;
        glow += texture(screen_texture, uv + vec2(texel.x * 2.5, 0.0)).r * 0.10;
        glow += texture(screen_texture, uv - vec2(texel.x * 2.5, 0.0)).r * 0.10;
        glow += texture(screen_texture, uv + vec2(0.0, texel.y * 1.0)).r * 0.09;
        glow += texture(screen_texture, uv - vec2(0.0, texel.y * 1.0)).r * 0.09;
        vec3 glow_tint = max(color, text_fg);
        color += glow_tint * glow * 0.42 * monitor_bloom;

        float scanline = 0.88 + 0.12 * sin(uv.y * resolution.y * 3.14159265);
        color *= mix(1.0, scanline, clamp(monitor_scanline, 0.0, 1.6));

        vec2 maskpix = fract(uv * resolution);
        float slot = 0.95 + 0.05 * smoothstep(0.08, 0.55, maskpix.x) * (1.0 - smoothstep(0.72, 0.98, maskpix.x));
        color *= mix(1.0, slot, clamp(monitor_mask, 0.0, 1.6));

        vec2 vig = uv * (1.0 - uv);
        float vig_mul = clamp(pow(vig.x * vig.y * 20.0, 0.10), 0.60, 1.0);
        color *= mix(1.0, vig_mul, clamp(monitor_vignette, 0.0, 1.6));

        float noise = (hash21(uv * resolution + time_sec * 12.0) - 0.5) * 0.003;
        color += vec3(noise);

        color = (color - vec3(0.5)) * monitor_contrast + vec3(0.5);
        color *= monitor_brightness;
        color = clamp(color, vec3(0.0), vec3(1.0));
        FragColor = vec4(color, 1.0);
        return;
    }

    // CRT bloom/halation: horizontal smear + local blur.
    float bloom = 0.0;
    bloom += crt_sample(uv) * 0.28;
    bloom += crt_sample(uv + vec2(texel.x * 1.0, 0.0)) * 0.17;
    bloom += crt_sample(uv - vec2(texel.x * 1.0, 0.0)) * 0.17;
    bloom += crt_sample(uv + vec2(texel.x * 2.5, 0.0)) * 0.10;
    bloom += crt_sample(uv - vec2(texel.x * 2.5, 0.0)) * 0.10;
    bloom += crt_sample(uv + vec2(0.0, texel.y * 1.0)) * 0.09;
    bloom += crt_sample(uv - vec2(0.0, texel.y * 1.0)) * 0.09;

    float beam = pow(pixel, 0.78) * 1.45;
    float trail = crt_sample(uv - vec2(texel.x * 2.0, 0.0)) * 0.18;
    float bloom_weight = 1.0;
    if (monitor_mode == 0 || monitor_mode == 1) {
        bloom_weight = smoothstep(0.20, 0.85, pixel);
    }
    vec3 color = phosphor_core * beam;
    color += phosphor_glow * (bloom * 1.22 * monitor_bloom + trail * 0.25) * bloom_weight;

    // Stable scanlines (removed time-drift to avoid visible blinking).
    float scanline = 0.88 + 0.12 * sin(uv.y * resolution.y * 3.14159265);
    color *= mix(1.0, scanline, clamp(monitor_scanline, 0.0, 1.6));

    // Slot mask / phosphor grille.
    vec2 maskpix = fract(uv * resolution);
    float slot = 0.95 + 0.05 * smoothstep(0.08, 0.55, maskpix.x) * (1.0 - smoothstep(0.72, 0.98, maskpix.x));
    color *= mix(1.0, slot, clamp(monitor_mask, 0.0, 1.6));

    // Very mild analog shimmer/noise.
    float flicker = 0.998 + 0.002 * sin(time_sec * 7.0);
    float noise = (hash21(uv * resolution + time_sec * 12.0) - 0.5) * 0.003;
    color *= flicker;
    color += vec3(noise);

    // Vignette (darker edges, kept gentle so corner text stays readable).
    vec2 vig = uv * (1.0 - uv);
    float vig_mul = clamp(pow(vig.x * vig.y * 20.0, 0.10), 0.60, 1.0);
    color *= mix(1.0, vig_mul, clamp(monitor_vignette, 0.0, 1.6));

    // Ambient glass lift.
    color += glass_ambient;

    // Keep thin AVDC glyphs visible.
    color = min(color * 1.16, vec3(1.0));
    color = (color - vec3(0.5)) * monitor_contrast + vec3(0.5);
    color *= monitor_brightness;
    color *= edge_mask;
    color = clamp(color, vec3(0.0), vec3(1.0));

    FragColor = vec4(color, 1.0);
}
)";

void display::init()
{
    // Source texture (monochrome framebuffer, single channel)
    glGenTextures(1, &source_tex_);
    glBindTexture(GL_TEXTURE_2D, source_tex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, FB_W, FB_H, 0,
                 GL_RED, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // CRT output texture (RGBA, post-shader)
    glGenTextures(1, &crt_tex_);
    glBindTexture(GL_TEXTURE_2D, crt_tex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, FB_W, FB_H, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // Framebuffer object for CRT render pass
    glGenFramebuffers(1, &fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, crt_tex_, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::cerr << "[error] CRT framebuffer incomplete\n";
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    bool disable_shader = false;
    if (const char *force = std::getenv("IDP_EMU_NO_SHADER"))
        disable_shader = force[0] != '\0' && force[0] != '0';

    if (disable_shader)
    {
        shader_ = 0;
        std::cerr << "[warning] IDP_EMU_NO_SHADER=1 -> using raw framebuffer output\n";
    }
    else
    {
        // CRT shader
        shader_ = compile_shader(crt_vert_src, crt_frag_src);
        if (!shader_)
            std::cerr << "[warning] CRT/LCD shader unavailable, using raw framebuffer output\n";
    }

    if (shader_)
    {
        // Fullscreen quad (position + texcoord)
        float quad[] = {
            -1.0f, -1.0f, 0.0f, 0.0f,
             1.0f, -1.0f, 1.0f, 0.0f,
            -1.0f,  1.0f, 0.0f, 1.0f,
             1.0f,  1.0f, 1.0f, 1.0f,
        };
        glGenVertexArrays(1, &vao_);
        glGenBuffers(1, &vbo_);
        glBindVertexArray(vao_);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                              (void *)(2 * sizeof(float)));
        glBindVertexArray(0);
    }
}

void display::shutdown()
{
    if (shader_) glDeleteProgram(shader_);
    if (fbo_) glDeleteFramebuffers(1, &fbo_);
    if (source_tex_) glDeleteTextures(1, &source_tex_);
    if (crt_tex_) glDeleteTextures(1, &crt_tex_);
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
}

bool display::load_font(const std::string &path)
{
    uint8_t rom[480]{};
    bool loaded = false;

    std::ifstream file(path, std::ios::binary);
    if (file)
    {
        file.read(reinterpret_cast<char *>(rom), 480);
        loaded = (bool)file;
    }
    if (!loaded)
    {
        const uint32_t n = ef9367_builtin_font::rom_size < 480u
            ? ef9367_builtin_font::rom_size : 480u;
        memcpy(rom, ef9367_builtin_font::rom, n);
        loaded = true;
        std::cout << "[info] Font loaded from embedded EF9365 ROM image\n";
    }

    // Decode 96 characters, each 5 bytes (40 bits), first 35 bits = 7 rows x 5 cols
    for (int ch = 0; ch < 96; ch++)
    {
        const uint8_t *src = rom + ch * 5;
        // Combine 5 bytes into 40-bit value (big-endian)
        uint64_t bits = 0;
        for (int b = 0; b < 5; b++)
            bits = (bits << 8) | src[b];

        // Extract 7 rows of 5 bits each
        for (int r = 0; r < 7; r++)
        {
            int shift = 35 - r * 5;
            font_5x7_[ch][r] = (bits >> shift) & 0x1F;
        }
    }

    font_loaded_ = true;
    if (file)
        std::cout << "[info] Font loaded: " << path << "\n";
    return true;
}

void display::update()
{
    if (phosphor_ == phosphor_type::color || phosphor_ == phosphor_type::flat) {
        // Preserve pixel codes exactly: color mode keeps indexed values,
        // flat mode has no phosphor persistence by design.
        memcpy(ghost_fb_, fb_, sizeof(fb_));
    } else {
        const uint16_t base_decay = (phosphor_ == phosphor_type::lcd) ? 214u : 224u;
        const float persistence = clampf(monitor_persistence_, 0.20f, 1.15f);
        uint16_t decay = (uint16_t)(base_decay * persistence);
        if (decay > 255u)
            decay = 255u;
        for (size_t i = 0; i < (size_t)(FB_W * FB_H); i++)
        {
            const uint8_t cur = fb_[i];
            if (cur >= 240u) {
                // Keep indexed AVDC text pixels exact even on mono CRT modes.
                ghost_fb_[i] = cur;
                continue;
            }
            const uint8_t prev = ghost_fb_[i];
            const uint8_t prev_mono = (prev >= 240u) ? 0u : prev;
            const uint8_t decayed = (uint8_t)(((uint16_t)prev_mono * decay) >> 8);
            ghost_fb_[i] = (cur > decayed) ? cur : decayed;
        }
    }

    glBindTexture(GL_TEXTURE_2D, source_tex_);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, FB_W, FB_H,
                    GL_RED, GL_UNSIGNED_BYTE, ghost_fb_);

    if (shader_)
        apply_crt();
}

void display::apply_crt()
{
    if (!shader_)
        return;

    // Save GL state
    GLint prev_fbo, prev_viewport[4];
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fbo);
    glGetIntegerv(GL_VIEWPORT, prev_viewport);

    // Render to CRT FBO
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glViewport(0, 0, FB_W, FB_H);

    glUseProgram(shader_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, source_tex_);
    glUniform1i(glGetUniformLocation(shader_, "screen_texture"), 0);
    glUniform2f(glGetUniformLocation(shader_, "resolution"),
                (float)FB_W, (float)FB_H);
    glUniform1f(glGetUniformLocation(shader_, "time_sec"),
                (float)SDL_GetTicks() * 0.001f);
    glUniform1i(glGetUniformLocation(shader_, "monitor_mode"), (int)phosphor_);
    glUniform1f(glGetUniformLocation(shader_, "monitor_brightness"), monitor_brightness_);
    glUniform1f(glGetUniformLocation(shader_, "monitor_contrast"), monitor_contrast_);
    glUniform1f(glGetUniformLocation(shader_, "monitor_bloom"), monitor_bloom_);
    glUniform1f(glGetUniformLocation(shader_, "monitor_scanline"), monitor_scanline_strength_);
    glUniform1f(glGetUniformLocation(shader_, "monitor_mask"), monitor_mask_strength_);
    glUniform1f(glGetUniformLocation(shader_, "monitor_vignette"), monitor_vignette_);
    glUniform3f(glGetUniformLocation(shader_, "text_fg"), text_fg_r_, text_fg_g_, text_fg_b_);
    glUniform3f(glGetUniformLocation(shader_, "text_bg"), text_bg_r_, text_bg_g_, text_bg_b_);
    glUniform1f(glGetUniformLocation(shader_, "text_reverse"), text_reverse_video_ ? 1.0f : 0.0f);
    glUniform1f(glGetUniformLocation(shader_, "text_force_background"), text_force_background_ ? 1.0f : 0.0f);
    if (phosphor_ == phosphor_type::flat) {
        // Clean green-on-black; intensity comes straight from the framebuffer.
        glUniform3f(glGetUniformLocation(shader_, "phosphor_core"), 0.45f, 1.00f, 0.50f);
        glUniform3f(glGetUniformLocation(shader_, "phosphor_glow"), 0.0f, 0.0f, 0.0f);
        glUniform3f(glGetUniformLocation(shader_, "glass_ambient"), 0.0f, 0.0f, 0.0f);
    } else if (phosphor_ == phosphor_type::orange) {
        glUniform3f(glGetUniformLocation(shader_, "phosphor_core"), 1.72f, 0.83f, 0.22f);
        glUniform3f(glGetUniformLocation(shader_, "phosphor_glow"), 1.05f, 0.48f, 0.12f);
        glUniform3f(glGetUniformLocation(shader_, "glass_ambient"), 0.024f, 0.012f, 0.004f);
    } else if (phosphor_ == phosphor_type::color) {
        glUniform3f(glGetUniformLocation(shader_, "phosphor_core"), 0.92f, 0.92f, 0.92f);
        glUniform3f(glGetUniformLocation(shader_, "phosphor_glow"), 0.62f, 0.62f, 0.62f);
        glUniform3f(glGetUniformLocation(shader_, "glass_ambient"), 0.012f, 0.012f, 0.012f);
    } else if (phosphor_ == phosphor_type::lcd) {
        // Unused in LCD mode but still set for completeness.
        glUniform3f(glGetUniformLocation(shader_, "phosphor_core"), 0.0f, 0.0f, 0.0f);
        glUniform3f(glGetUniformLocation(shader_, "phosphor_glow"), 0.0f, 0.0f, 0.0f);
        glUniform3f(glGetUniformLocation(shader_, "glass_ambient"), 0.0f, 0.0f, 0.0f);
    } else {
        glUniform3f(glGetUniformLocation(shader_, "phosphor_core"), 0.29f, 1.62f, 0.36f);
        glUniform3f(glGetUniformLocation(shader_, "phosphor_glow"), 0.11f, 0.92f, 0.24f);
        glUniform3f(glGetUniformLocation(shader_, "glass_ambient"), 0.008f, 0.022f, 0.008f);
    }

    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);

    // Restore GL state
    glUseProgram(0);
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prev_fbo);
    glViewport(prev_viewport[0], prev_viewport[1],
               prev_viewport[2], prev_viewport[3]);
}

void display::set_monitor_brightness(float v)
{
    monitor_brightness_ = clampf(v, 0.35f, 2.20f);
}

void display::set_monitor_contrast(float v)
{
    monitor_contrast_ = clampf(v, 0.40f, 2.30f);
}

void display::set_monitor_bloom(float v)
{
    monitor_bloom_ = clampf(v, 0.00f, 2.40f);
}

void display::set_monitor_scanline_strength(float v)
{
    monitor_scanline_strength_ = clampf(v, 0.00f, 1.60f);
}

void display::set_monitor_mask_strength(float v)
{
    monitor_mask_strength_ = clampf(v, 0.00f, 1.60f);
}

void display::set_monitor_vignette(float v)
{
    monitor_vignette_ = clampf(v, 0.00f, 1.60f);
}

void display::set_monitor_persistence(float v)
{
    monitor_persistence_ = clampf(v, 0.20f, 1.15f);
}

void display::reset_monitor_tuning()
{
    monitor_brightness_ = 1.00f;
    monitor_contrast_ = 1.00f;
    monitor_bloom_ = 1.00f;
    monitor_scanline_strength_ = 1.00f;
    monitor_mask_strength_ = 1.00f;
    monitor_vignette_ = 1.00f;
    monitor_persistence_ = 0.78f;
}

void display::set_text_palette_rgb(float fg_r, float fg_g, float fg_b,
                                   float bg_r, float bg_g, float bg_b,
                                   bool reverse_video, bool force_background)
{
    text_fg_r_ = clampf(fg_r, 0.0f, 1.0f);
    text_fg_g_ = clampf(fg_g, 0.0f, 1.0f);
    text_fg_b_ = clampf(fg_b, 0.0f, 1.0f);
    text_bg_r_ = clampf(bg_r, 0.0f, 1.0f);
    text_bg_g_ = clampf(bg_g, 0.0f, 1.0f);
    text_bg_b_ = clampf(bg_b, 0.0f, 1.0f);
    text_reverse_video_ = reverse_video;
    text_force_background_ = force_background;
}

void display::set_pixel(int x, int y, bool on)
{
    if (x >= 0 && x < FB_W && y >= 0 && y < FB_H)
        fb_[x + y * FB_W] = on ? 255 : 0;
}

void display::add_pixel(int x, int y, uint8_t value)
{
    if (x < 0 || x >= FB_W || y < 0 || y >= FB_H)
        return;
    uint8_t &dst = fb_[x + y * FB_W];
    const uint16_t sum = (uint16_t)dst + (uint16_t)value;
    dst = (sum > 255u) ? 255u : (uint8_t)sum;
}

void display::set_level_pixel(int x, int y, uint8_t value)
{
    if (x < 0 || x >= FB_W || y < 0 || y >= FB_H)
        return;
    fb_[x + y * FB_W] = value;
}

void display::set_index_pixel(int x, int y, uint8_t index4)
{
    if (x < 0 || x >= FB_W || y < 0 || y >= FB_H)
        return;
    fb_[x + y * FB_W] = (uint8_t)(0xF0u | (index4 & 0x0Fu));
}

void display::clear()
{
    memset(fb_, 0, sizeof(fb_));
}

void display::clear_all()
{
    memset(fb_, 0, sizeof(fb_));
    memset(ghost_fb_, 0, sizeof(ghost_fb_));
}

void display::draw_char(int col, int row, char c)
{
    if (col < 0 || col >= COLS || row < 0 || row >= ROWS)
        return;
    int idx = (unsigned char)c - 32;
    if (idx < 0 || idx >= 96)
        return;

    if (!font_loaded_)
        return;

    int x0 = col * CHAR_W;
    int y0 = row * CHAR_H;

    for (int r = 0; r < 7; r++)
    {
        uint8_t bits = font_5x7_[idx][r];
        for (int b = 0; b < 5; b++)
        {
            if (bits & (0x10 >> b))
            {
                // Fill 2x2 block for each font pixel
                int px = x0 + b * 2;
                int py = y0 + r * 2;
                set_pixel(px, py, true);
                set_pixel(px + 1, py, true);
                set_pixel(px, py + 1, true);
                set_pixel(px + 1, py + 1, true);
            }
        }
    }
}

void display::draw_text(int col, int row, const char *text)
{
    while (*text && col < COLS)
    {
        draw_char(col, row, *text);
        col++;
        text++;
    }
}

void display::fill_char_cell(int col, int row)
{
    if (col < 0 || col >= COLS || row < 0 || row >= ROWS)
        return;

    const int x0 = col * CHAR_W;
    const int y0 = row * CHAR_H;
    for (int y = y0; y < y0 + CHAR_H; y++)
    {
        for (int x = x0; x < x0 + CHAR_W; x++)
            set_pixel(x, y, true);
    }
}

GLuint display::compile_shader(const char *vert_src, const char *frag_src)
{
    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    if (!vs)
    {
        std::cerr << "[error] glCreateShader(GL_VERTEX_SHADER) failed\n";
        return 0;
    }
    glShaderSource(vs, 1, &vert_src, nullptr);
    glCompileShader(vs);
    GLint ok = 0;
    glGetShaderiv(vs, GL_COMPILE_STATUS, &ok);
    const bool vs_ok = ok == GL_TRUE;
    if (!ok)
    {
        char log[512];
        glGetShaderInfoLog(vs, sizeof(log), nullptr, log);
        std::cerr << "[error] Vertex shader: " << log << "\n";
    }

    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    if (!fs)
    {
        std::cerr << "[error] glCreateShader(GL_FRAGMENT_SHADER) failed\n";
        glDeleteShader(vs);
        return 0;
    }
    glShaderSource(fs, 1, &frag_src, nullptr);
    glCompileShader(fs);
    glGetShaderiv(fs, GL_COMPILE_STATUS, &ok);
    const bool fs_ok = ok == GL_TRUE;
    if (!ok)
    {
        char log[512];
        glGetShaderInfoLog(fs, sizeof(log), nullptr, log);
        std::cerr << "[error] Fragment shader: " << log << "\n";
    }

    if (!vs_ok || !fs_ok)
    {
        glDeleteShader(vs);
        glDeleteShader(fs);
        return 0;
    }

    GLuint prog = glCreateProgram();
    if (!prog)
    {
        std::cerr << "[error] glCreateProgram failed\n";
        glDeleteShader(vs);
        glDeleteShader(fs);
        return 0;
    }
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        char log[512];
        glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
        std::cerr << "[error] Shader link: " << log << "\n";
    }

    glDeleteShader(vs);
    glDeleteShader(fs);
    if (!ok)
    {
        glDeleteProgram(prog);
        return 0;
    }
    return prog;
}
