/*
* Vulkan Example - OpenGL interoperability example
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#include <common.hpp>
#include <glfw/glfw.hpp>
#include <glad/glad.h>
#include <utils.hpp>
#include <vks/filesystem.hpp>
#include "csscolors.hpp"

#define SPARSE_2D_ARRAY

class GLTexturesArrayTest {
public:
    void run();

private:
    void init();
    void draw();
    void destroy();

    GLuint vao{ 0 };
    GLuint program{ 0 };
    GLuint sampler{ 0 };
    GLuint paramsBuffer{ 0 };
    double startTime{ 0.0 };

#define TEXTURE_INTERNAL_FORMAT GL_RGBA8
#define TEXTURE_FORMAT GL_RGBA

#ifdef SPARSE_2D_ARRAY
    GLuint colorTexturesArray{ 0 };
#endif 
#ifdef BINDLESS
    GLuint texturesBuffer{ 0 }
    std::vector<GLuint> colorTextures;
    std::vector<uint64_t> colorTextureHandles;
#endif
    glfw::Window window;
    glm::uvec2 dimensions{ 512, 512 };

    static const uint32_t COLOR_COUNT;
    static const uint32_t COLOR_SIZE;
    static const uint32_t PARAMS_SIZE = (uint32_t)sizeof(glm::vec4);

};

const uint32_t GLTexturesArrayTest::COLOR_COUNT = (uint32_t)CSS_COLORS.size();
const uint32_t GLTexturesArrayTest::COLOR_SIZE = (uint32_t)(sizeof(uint64_t) * COLOR_COUNT);

static void glfwErrorCallback(int, const char* message) {
    std::cerr << message << std::endl;
}

static const std::string& localPath() {
    static std::string result;
    static std::once_flag once;
    std::call_once(once, [&]{
        result = __FILE__;
        auto pos = result.rfind('/');
        result = result.substr(0, pos);
    });
    return result;
}
static bool SPARSE_SUPPORT = false;

void GLTexturesArrayTest::init() {
    if (!glfw::Window::init()) {
        throw std::runtime_error("Could not initialize GLFW");
    }
    glfwSetErrorCallback(glfwErrorCallback);
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
    // 4.1 to ensure mac compatibility
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);

    // Window doesn't need to be large, it only exists to give us a GL context
    window.createWindow(dimensions);
    window.setTitle("OpenGL 4.6");
    window.makeCurrent();

    startTime = glfwGetTime();

    gl::init();

    std::cout << "GL Version: " << (const char*)glGetString(GL_VERSION) << std::endl;
    std::cout << "GL Shader Language Version: " << (const char*)glGetString(GL_SHADING_LANGUAGE_VERSION) << std::endl;
    std::cout << "GL Vendor: " << (const char*)glGetString(GL_VENDOR) << std::endl;
    std::cout << "GL Renderer: " << (const char*)glGetString(GL_RENDERER) << std::endl;

    gl::setupDebugLogging();
    glfwSetErrorCallback(glfwErrorCallback);

    SPARSE_SUPPORT = glTexturePageCommitmentEXT != nullptr;
    
    glGenSamplers(1, &sampler);
    glSamplerParameteri(sampler, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glSamplerParameteri(sampler, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glSamplerParameteri(sampler, GL_TEXTURE_COMPARE_MODE, GL_NONE);
    glSamplerParameteri(sampler, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glSamplerParameteri(sampler, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glSamplerParameteri(sampler, GL_TEXTURE_WRAP_R, GL_REPEAT);
    //glSamplerParameterf(sampler, GL_TEXTURE_MIN_LOD, sampler.getMinMip());
    //glSamplerParameterf(sampler, GL_TEXTURE_MAX_LOD, (sampler.getMaxMip() == Sampler::MAX_MIP_LEVEL ? 1000.f : sampler.getMaxMip()));

#ifdef SPARSE_2D_ARRAY
    GLint count = 0;
    glGetInternalformativ(GL_TEXTURE_2D_ARRAY, TEXTURE_INTERNAL_FORMAT, GL_NUM_VIRTUAL_PAGE_SIZES_ARB, 1, &count);
    std::vector<glm::uvec3> pageSizes;
    if (count > 0) {
        std::vector<GLint> x, y, z;
        x.resize(count);
        glGetInternalformativ(GL_TEXTURE_2D_ARRAY, TEXTURE_INTERNAL_FORMAT, GL_VIRTUAL_PAGE_SIZE_X_ARB, 1, &x[0]);
        y.resize(count);
        glGetInternalformativ(GL_TEXTURE_2D_ARRAY, TEXTURE_INTERNAL_FORMAT, GL_VIRTUAL_PAGE_SIZE_Y_ARB, 1, &y[0]);
        z.resize(count);
        glGetInternalformativ(GL_TEXTURE_2D_ARRAY, TEXTURE_INTERNAL_FORMAT, GL_VIRTUAL_PAGE_SIZE_Z_ARB, 1, &z[0]);

        pageSizes.resize(count);
        for (GLint i = 0; i < count; ++i) {
            pageSizes[i] = glm::uvec3(x[i], y[i], z[i]);
        }
    }

#endif

#ifdef SPARSE_2D_ARRAY
    if (SPARSE_SUPPORT) {
        glCreateTextures(GL_TEXTURE_2D_ARRAY, 1, &colorTexturesArray);
        glTextureParameteri(colorTexturesArray, GL_TEXTURE_SPARSE_ARB, GL_TRUE);
        glTextureParameteri(colorTexturesArray, GL_VIRTUAL_PAGE_SIZE_INDEX_ARB, 0);
        glTextureStorage3D(colorTexturesArray, 1, TEXTURE_INTERNAL_FORMAT, 1, 1, 512);
    } else {
        glGenTextures(1,&colorTexturesArray);
        glBindTexture(GL_TEXTURE_2D_ARRAY,colorTexturesArray);
        glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, 1, 1, 512, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    }

#endif

    {
        size_t count = CSS_COLORS.size();
#ifdef BINDLESS
        colorTextures.resize(count);
        colorTextureHandles.resize(count);
        glCreateTextures(GL_TEXTURE_2D, (GLsizei)count, colorTextures.data());
#endif

        for (size_t i = 0; i < count; ++i) {
            const auto& colorHex = CSS_COLORS[i].second;
            vec4 color{ 255.0f };
            for (size_t o = 0; o < 3; ++o) {
                std::string c = colorHex.substr(o * 2, 2);
                color[(uint8_t)o] = (float)std::stoi(c, nullptr, 16);
            }
            color /= 255.0f;
#ifdef BINDLESS
            glTextureStorage2D(colorTextures[i], 1, GL_RGBA8, 1, 1);
            glTextureSubImage2D(colorTextures[i], 0, 0, 0, 1, 1, GL_RGBA, GL_FLOAT, &color);
            colorTextureHandles[i] = glGetTextureSamplerHandleARB(colorTextures[i], sampler);
            glMakeTextureHandleResidentARB(colorTextureHandles[i]);
#endif
#ifdef SPARSE_2D_ARRAY
            const GLint mip = 0;
            const glm::ivec3 offset{ 0, 0, i };
            const glm::uvec3 size{ 1 };
            if (SPARSE_SUPPORT){
                
                glTexturePageCommitmentEXT(colorTexturesArray, mip, offset.x, offset.y, offset.z, size.x, size.y, size.z, GL_TRUE);
                glTextureSubImage3D(colorTexturesArray, mip, offset.x, offset.y, offset.z, size.x, size.y, size.z, GL_RGBA, GL_FLOAT, &color);
            } else {
                glTexSubImage3D(GL_TEXTURE_2D_ARRAY, mip, offset.x, offset.y, offset.z, size.x, size.y, size.z, GL_RGBA, GL_FLOAT, &color);
            }
#endif
        }
    }
#ifdef BINDLESS
    glCreateBuffers(1, &texturesBuffer);
    glNamedBufferStorage(texturesBuffer, COLOR_SIZE, colorTextureHandles.data(), 0);
    glBindBufferRange(GL_UNIFORM_BUFFER, 0, texturesBuffer, 0, sizeof(uint64_t) * COLOR_COUNT);
#endif

    glBindTexture(GL_TEXTURE_2D_ARRAY, colorTexturesArray);

#if 0
    auto texturesLoc = glGetUniformLocation(program, "textures");
    for (uint32_t i = 0; i < 32; ++i) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, colorTextures[i]);
    }
#endif

#ifdef __APPLE__
    glGenBuffers(1, &paramsBuffer);
    glBindBuffer(GL_UNIFORM_BUFFER, paramsBuffer);
    glBufferData(GL_UNIFORM_BUFFER, PARAMS_SIZE, nullptr, GL_DYNAMIC_DRAW);
#else
    glCreateBuffers(1, &paramsBuffer);
    glNamedBufferStorage(paramsBuffer, PARAMS_SIZE, nullptr, GL_DYNAMIC_STORAGE_BIT);
#endif
    glBindBufferRange(GL_UNIFORM_BUFFER, 0, paramsBuffer, 0, PARAMS_SIZE);

    // The remaining initialization code is all standard OpenGL
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);

#ifdef __APPLE__
    auto vertexSource = vks::file::readTextFile(localPath() + "/gltexturesarray.vert");
    auto vertexShader = gl::loadShader(vertexSource, GL_VERTEX_SHADER);
    auto fragmentSource = vks::file::readTextFile(localPath() + "/gltexturesarray.frag");
    auto fragmentShader = gl::loadShader(fragmentSource, GL_FRAGMENT_SHADER);
    program = gl::buildProgram(vertexShader, fragmentShader);
#else
    auto vertexSpirv = vks::file::readSpirvFile(localPath() + "/gltexturesarray.vert.spv");
    auto vertexShader = gl::loadSpirvShader(vertexSpirv, GL_VERTEX_SHADER);
    auto fragmentSpirv = vks::file::readSpirvFile(localPath() + "/gltexturesarray.frag.spv");
    auto fragmentShader = gl::loadSpirvShader(fragmentSpirv, GL_FRAGMENT_SHADER);
    program = gl::buildProgram(vertexShader, fragmentShader);
#endif
}

void GLTexturesArrayTest::destroy() {
    glBindVertexArray(0);
    glUseProgram(0);
    glDeleteVertexArrays(1, &vao);
    glDeleteProgram(program);
    glFlush();
    glFinish();
    window.destroyWindow();
}

void GLTexturesArrayTest::draw() {
    // Basic GL rendering code to render animated noise to a texture
    glUseProgram(program);
    glViewport(0, 0, dimensions.x, dimensions.y);

    static uint32_t  textureIndex = 0;
    textureIndex = ((textureIndex + 1) % COLOR_COUNT);
    glm::vec4 params{ dimensions, (float)(textureIndex), (float)(glfwGetTime() - startTime) };
#ifdef __APPLE__
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(glm::vec4), &params);
#else
    glNamedBufferSubData(paramsBuffer, 0, sizeof(glm::vec4), &params);
#endif
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    window.present();
}

void GLTexturesArrayTest::run() {
    init();
    window.runWindowLoop([this] { draw(); });
    destroy();
}

RUN_EXAMPLE(GLTexturesArrayTest)
