#include "gl.hpp"

#if !defined(__ANDROID__)
#include <mutex>

#if defined(WIN32)
typedef PROC(APIENTRYP PFNWGLGETPROCADDRESS)(LPCSTR);
PFNWGLGETPROCADDRESS glad_wglGetProcAddress;
#define wglGetProcAddress glad_wglGetProcAddress

static void* getGlProcessAddress(const char* namez) {
    static HMODULE glModule = nullptr;
    if (!glModule) {
        glModule = LoadLibraryW(L"opengl32.dll");
        glad_wglGetProcAddress = (PFNWGLGETPROCADDRESS)GetProcAddress(glModule, "wglGetProcAddress");
    }

    auto result = wglGetProcAddress(namez);
    if (!result) {
        result = GetProcAddress(glModule, namez);
    }
    if (!result) {
        OutputDebugStringA(namez);
        OutputDebugStringA("\n");
    }
    return (void*)result;
}
#endif

void gl::init() {
    static std::once_flag once;
    std::call_once(once, [] { gladLoadGL(); });
}

void gl::shaderCompileCheck(GLuint shader) {
    GLint isCompiled = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &isCompiled);
    if (isCompiled == GL_FALSE) {
        GLint maxLength = 0;
        std::array<GLchar, 8192> log;
        //glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &maxLength);

        // The maxLength includes the NULL character
        glGetShaderInfoLog(shader, 8192, &maxLength, log.data());
        std::string strError;
        strError.insert(strError.end(), log.begin(), log.begin() + maxLength - 1);

        // Provide the infolog in whatever manor you deem best.
        // Exit with failure.
        glDeleteShader(shader);  // Don't leak the shader.
        throw std::runtime_error("Shader compiled failed");
    }
}

void gl::programLinkCheck(GLuint program) {
    GLint isLinked = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &isLinked);
    if (isLinked == GL_FALSE) {
        GLint maxLength = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &maxLength);

        // The maxLength includes the NULL character
        std::vector<GLchar> errorLog(maxLength);
        glGetProgramInfoLog(program, maxLength, &maxLength, &errorLog[0]);
        std::string strError;
        strError.insert(strError.end(), errorLog.begin(), errorLog.end());
        glDeleteProgram(program);  // Don't leak the shader.
        throw std::runtime_error("Shader compiled failed");
    }
}


GLuint gl::loadSpirvShader(const std::vector<uint32_t>& spirv, GLenum shaderType) {
    // Create the shader object.
    GLuint shader = glCreateShader(shaderType);
    // Load the SPIR-V module into the shader object
    glShaderBinary(1, &shader, GL_SHADER_BINARY_FORMAT_SPIR_V_ARB, spirv.data(), (GLsizei)(spirv.size() * sizeof(uint32_t)));
    GLuint constantIndex = 0;
    GLuint constantValue = 0;
    glSpecializeShaderARB(shader, "main", 0, &constantIndex, &constantValue);
    gl::shaderCompileCheck(shader);
    return shader;
}


GLuint gl::loadShader(const std::string& shaderSource, GLenum shaderType) {
    GLuint shader = glCreateShader(shaderType);
    int sizes = (int)shaderSource.size();
    const GLchar* strings = shaderSource.c_str();
    glShaderSource(shader, 1, &strings, &sizes);
    glCompileShader(shader);
    gl::shaderCompileCheck(shader);
    return shader;
}

GLuint gl::buildProgram(GLuint vertexShader, GLuint fragmentShader) {
    GLuint program = glCreateProgram();
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);
    // fixme error checking
    return program;

}

GLuint gl::buildProgram(const std::string& vertexShaderSource, const std::string& fragmentShaderSource) {
    GLuint vs = loadShader(vertexShaderSource, GL_VERTEX_SHADER);
    GLuint fs = loadShader(fragmentShaderSource, GL_FRAGMENT_SHADER);
    GLuint program = buildProgram(vs, fs);
    glDeleteShader(vs);
    glDeleteShader(fs);
    return program;
}

void gl::report() {
    std::cout << glGetString(GL_VENDOR) << std::endl;
    std::cout << glGetString(GL_RENDERER) << std::endl;
    std::cout << glGetString(GL_VERSION) << std::endl;
    for (const auto& extension : getExtensions()) {
        std::cout << "\t" << extension << std::endl;
    }
}

const std::set<std::string>& gl::getExtensions() {
    static std::set<std::string> extensions;
    static std::once_flag once;
    std::call_once(once, [&] {
        GLint n;
        glGetIntegerv(GL_NUM_EXTENSIONS, &n);
        if (n > 0) {
            GLint i;
            for (i = 0; i < n; i++) {
                extensions.insert((const char*)glGetStringi(GL_EXTENSIONS, i));
            }
        }
    });
    return extensions;
}

static void debugMessageCallback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const void* userParam) {
    if (GL_DEBUG_SEVERITY_NOTIFICATION == severity) {
        return;
    }
    // FIXME For high severity errors, force a sync to the log, since we might crash
    // before the log file was flushed otherwise.  Performance hit here
    std::cout << "OpenGL: " << message << std::endl;
}

void gl::setupDebugLogging() {
    if (glDebugMessageCallback) {
        glDebugMessageCallback(debugMessageCallback, NULL);
        glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS_ARB);
    }
}
#endif
