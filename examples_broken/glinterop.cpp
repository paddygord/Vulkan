#include <GL/glew.h>
#include "vulkanExampleBase.h"

using glm::ivec3;
using glm::ivec2;
using glm::uvec2;
using glm::mat3;
using glm::mat4;
using glm::vec2;
using glm::vec3;
using glm::vec4;
using glm::quat;


bool checkFramebufferStatus(GLenum target = GL_FRAMEBUFFER) {
    GLuint status = glCheckFramebufferStatus(target);
    switch (status) {
    case GL_FRAMEBUFFER_COMPLETE:
        return true;
        break;

    case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT:
        std::cerr << "framebuffer incomplete attachment" << std::endl;
        break;

    case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT:
        std::cerr << "framebuffer missing attachment" << std::endl;
        break;

    case GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER:
        std::cerr << "framebuffer incomplete draw buffer" << std::endl;
        break;

    case GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER:
        std::cerr << "framebuffer incomplete read buffer" << std::endl;
        break;

    case GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE:
        std::cerr << "framebuffer incomplete multisample" << std::endl;
        break;

    case GL_FRAMEBUFFER_INCOMPLETE_LAYER_TARGETS:
        std::cerr << "framebuffer incomplete layer targets" << std::endl;
        break;

    case GL_FRAMEBUFFER_UNSUPPORTED:
        std::cerr << "framebuffer unsupported internal format or image" << std::endl;
        break;

    default:
        std::cerr << "other framebuffer error" << std::endl;
        break;
    }

    return false;
}

bool checkGlError() {
    GLenum error = glGetError();
    if (!error) {
        return false;
    } else {
        switch (error) {
        case GL_INVALID_ENUM:
            std::cerr << ": An unacceptable value is specified for an enumerated argument.The offending command is ignored and has no other side effect than to set the error flag.";
            break;
        case GL_INVALID_VALUE:
            std::cerr << ": A numeric argument is out of range.The offending command is ignored and has no other side effect than to set the error flag";
            break;
        case GL_INVALID_OPERATION:
            std::cerr << ": The specified operation is not allowed in the current state.The offending command is ignored and has no other side effect than to set the error flag..";
            break;
        case GL_INVALID_FRAMEBUFFER_OPERATION:
            std::cerr << ": The framebuffer object is not complete.The offending command is ignored and has no other side effect than to set the error flag.";
            break;
        case GL_OUT_OF_MEMORY:
            std::cerr << ": There is not enough memory left to execute the command.The state of the GL is undefined, except for the state of the error flags, after this error is recorded.";
            break;
        case GL_STACK_UNDERFLOW:
            std::cerr << ": An attempt has been made to perform an operation that would cause an internal stack to underflow.";
            break;
        case GL_STACK_OVERFLOW:
            std::cerr << ": An attempt has been made to perform an operation that would cause an internal stack to overflow.";
            break;
        }
        return true;
    }
}

void glDebugCallbackHandler(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar *msg, GLvoid* data) {
    OutputDebugStringA(msg);
    std::cout << "debug call: " << msg << std::endl;
}

//////////////////////////////////////////////////////////////////////
//
// GLFW provides cross platform window creation
//

#include <GLFW/glfw3.h>

namespace glfw {
    inline GLFWwindow * createWindow(const uvec2 & size, const ivec2 & position = ivec2(INT_MIN)) {
        GLFWwindow * window = glfwCreateWindow(size.x, size.y, "glfw", nullptr, nullptr);
        if (!window) {
            throw std::runtime_error("Unable to create rendering window");
        }
        if ((position.x > INT_MIN) && (position.y > INT_MIN)) {
            glfwSetWindowPos(window, position.x, position.y);
        }
        return window;
    }
}

// A class to encapsulate using GLFW to handle input and render a scene
class GlfwApp {

protected:
    uvec2 windowSize;
    ivec2 windowPosition;
    GLFWwindow * window { nullptr };
    unsigned int frame { 0 };

public:
    GlfwApp() {
        // Initialize the GLFW system for creating and positioning windows
        if (!glfwInit()) {
            std::runtime_error("Failed to initialize GLFW");
        }
        glfwSetErrorCallback(ErrorCallback);
    }

    virtual ~GlfwApp() {
        if (nullptr != window) {
            glfwDestroyWindow(window);
        }
        glfwTerminate();
    }

    virtual int run() {
        preCreate();

        window = createRenderingTarget(windowSize, windowPosition);

        if (!window) {
            std::cout << "Unable to create OpenGL window" << std::endl;
            return -1;
        }

        postCreate();

        initGl();

        while (!glfwWindowShouldClose(window)) {
            ++frame;
            glfwPollEvents();
            update();
            draw();
            finishFrame();
        }

        shutdownGl();

        return 0;
    }


protected:
    virtual GLFWwindow * createRenderingTarget(uvec2 & size, ivec2 & pos) {
        size = uvec2(800, 600);
        pos = ivec2(100, 100);
        return glfw::createWindow(size, pos);
    };

    virtual void draw() = 0;

    void preCreate() {
        glfwWindowHint(GLFW_DEPTH_BITS, 16);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, true);
    }


    void postCreate() {
        glfwSetWindowUserPointer(window, this);
        glfwSetKeyCallback(window, KeyCallback);
        glfwSetMouseButtonCallback(window, MouseButtonCallback);
        glfwMakeContextCurrent(window);

        // Initialize the OpenGL bindings
        // For some reason we have to set this experminetal flag to properly
        // init GLEW if we use a core context.
        glewExperimental = GL_TRUE;
        if (0 != glewInit()) {
            std::runtime_error("Failed to initialize GLEW");
        }
        glGetError();

        if (GLEW_KHR_debug) {
            GLint v;
            glGetIntegerv(GL_CONTEXT_FLAGS, &v);
            if (v & GL_CONTEXT_FLAG_DEBUG_BIT) {
                glDebugMessageCallback((GLDEBUGPROC)glDebugCallbackHandler, this);
            }
        }
    }

    virtual void initGl() {
    }

    virtual void shutdownGl() {
    }

    virtual void finishFrame() {
        glfwSwapBuffers(window);
    }

    virtual void destroyWindow() {
        glfwSetKeyCallback(window, nullptr);
        glfwSetMouseButtonCallback(window, nullptr);
        glfwDestroyWindow(window);
    }

    virtual void onKey(int key, int scancode, int action, int mods) {
        if (GLFW_PRESS != action) {
            return;
        }

        switch (key) {
        case GLFW_KEY_ESCAPE:
            glfwSetWindowShouldClose(window, 1);
            return;
        }
    }

    virtual void update() {}

    virtual void onMouseButton(int button, int action, int mods) {}

protected:
    virtual void viewport(const ivec2 & pos, const uvec2 & size) {
        glViewport(pos.x, pos.y, size.x, size.y);
    }

private:

    static void KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
        GlfwApp * instance = (GlfwApp *)glfwGetWindowUserPointer(window);
        instance->onKey(key, scancode, action, mods);
    }

    static void MouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
        GlfwApp * instance = (GlfwApp *)glfwGetWindowUserPointer(window);
        instance->onMouseButton(button, action, mods);
    }

    static void ErrorCallback(int error, const char* description) {
        throw std::runtime_error(description);
    }
};


class OpenGLInterop : public GlfwApp, vkx::Context {
public:
    OpenGLInterop() {
        createContext(true);
    }
protected:

    void draw() override {
        glClearColor(0, 0.5f, 0.8f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
    }

};

RUN_EXAMPLE(OpenGLInterop)

