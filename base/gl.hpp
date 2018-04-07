#include "common.hpp"
#include <glad/glad.h>

namespace gl {
void init();
GLuint loadShader(const std::string& shaderSource, GLenum shaderType);
GLuint buildProgram(const std::string& vertexShaderSource, const std::string& fragmentShaderSource);
void report();
void setupDebugLogging();
}  // namespace gl
