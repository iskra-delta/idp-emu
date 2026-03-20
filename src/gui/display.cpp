#include "display.hpp"
#include <cstring>
#include <fstream>
#include <iostream>

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

void main() {
    vec2 uv = TexCoord;

    // Underscan: map screen edges slightly outside source so content fits inside
    // the visible CRT area even with barrel distortion.
    uv = (uv - 0.5) * 1.12 + 0.5;

    // Barrel distortion (CRT curvature)
    vec2 cc = uv - 0.5;
    float r2 = dot(cc, cc);
    uv = cc * (1.0 + 0.06 * r2 + 0.04 * r2 * r2) + 0.5;

    // Outside screen = black (rounded corners)
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
        FragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    // Sample pixel
    float pixel = texture(screen_texture, uv).r;

    // Bloom (soft glow from nearby lit pixels)
    float bloom = 0.0;
    vec2 texel = 1.0 / resolution;
    for (int i = -2; i <= 2; i++) {
        for (int j = -2; j <= 2; j++) {
            bloom += texture(screen_texture, uv + vec2(float(i), float(j)) * texel).r;
        }
    }
    bloom /= 25.0;

    // Green phosphor (Matsushita P1-style)
    vec3 color = vec3(0.14, 1.00, 0.26) * pixel;
    color += vec3(0.09, 0.50, 0.16) * bloom * 0.55;

    // Scanlines
    float scanline = sin(uv.y * resolution.y * 3.14159265) * 0.5 + 0.5;
    color *= 0.86 + 0.20 * scanline;

    // Vignette (darker edges)
    vec2 vig = uv * (1.0 - uv);
    color *= clamp(pow(vig.x * vig.y * 15.0, 0.20), 0.0, 1.0);

    // Ambient CRT glass glow
    color += vec3(0.005, 0.012, 0.005);

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
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
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

    // CRT shader
    shader_ = compile_shader(crt_vert_src, crt_frag_src);

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
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return false;

    uint8_t rom[480];
    file.read(reinterpret_cast<char *>(rom), 480);
    if (!file)
        return false;

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
    std::cout << "[info] Font loaded: " << path << "\n";
    return true;
}

void display::update()
{
    glBindTexture(GL_TEXTURE_2D, source_tex_);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, FB_W, FB_H,
                    GL_RED, GL_UNSIGNED_BYTE, fb_);

    apply_crt();
}

void display::apply_crt()
{
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

    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);

    // Restore GL state
    glUseProgram(0);
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prev_fbo);
    glViewport(prev_viewport[0], prev_viewport[1],
               prev_viewport[2], prev_viewport[3]);
}

void display::set_pixel(int x, int y, bool on)
{
    if (x >= 0 && x < FB_W && y >= 0 && y < FB_H)
        fb_[x + y * FB_W] = on ? 255 : 0;
}

void display::clear()
{
    memset(fb_, 0, sizeof(fb_));
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
    glShaderSource(vs, 1, &vert_src, nullptr);
    glCompileShader(vs);
    GLint ok;
    glGetShaderiv(vs, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        char log[512];
        glGetShaderInfoLog(vs, sizeof(log), nullptr, log);
        std::cerr << "[error] Vertex shader: " << log << "\n";
    }

    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &frag_src, nullptr);
    glCompileShader(fs);
    glGetShaderiv(fs, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        char log[512];
        glGetShaderInfoLog(fs, sizeof(log), nullptr, log);
        std::cerr << "[error] Fragment shader: " << log << "\n";
    }

    GLuint prog = glCreateProgram();
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
    return prog;
}
