#include "common.hpp"

#if !defined(__ANDROID__)
#include <glad/glad.h>

namespace gl {
void init();

void shaderCompileCheck(GLuint shader);
void programLinkCheck(GLuint program);
GLuint loadShader(const std::string& shaderSource, GLenum shaderType);
GLuint loadSpirvShader(const std::vector<uint32_t>& , GLenum shaderType);
GLuint buildProgram(const std::string& vertexShaderSource, const std::string& fragmentShaderSource);
GLuint buildProgram(GLuint vertexShader, GLuint fragmentShader);

void report();
const std::set<std::string>& getExtensions();
void setupDebugLogging();
}  // namespace gl

#endif
