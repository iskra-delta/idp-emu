// Keep Dear ImGui on the same OpenGL loader as the Windows application.
// GLEW is initialized in Gui::init() after SDL makes the context current.
#include <GL/glew.h>

#define IMGUI_IMPL_OPENGL_LOADER_CUSTOM
#include "imgui_impl_opengl3.cpp"
