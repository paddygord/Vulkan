#include "common.hpp"
#include "vulkanShapes.hpp"
#include "vulkanGL.hpp"

class VrExampleBase {
public:
    vkx::Context vulkanContext;
    vkx::ShapesRenderer vulkanRenderer;
    GLFWwindow* window{ nullptr };
    float fpsTimer{ 0 };
    float lastFPS{ 0 };
    uint32_t frameCounter{ 0 };
    glm::uvec2 size{ 1280, 720 };
    glm::uvec2 renderTargetSize;
    std::array<glm::mat4, 2> eyeViews;
    std::array<glm::mat4, 2> eyeProjections;
    GLuint _fbo{ 0 };
    GLuint _colorBuffer{ 0 };


    VrExampleBase() : vulkanRenderer{ vulkanContext, true } {
        glfwInit();
    }

    ~VrExampleBase() {
        if (nullptr != window) {
            glfwDestroyWindow(window);
        }
        glfwTerminate();
    }

    virtual void setupVrFramebuffer() {
        // Set up the framebuffer object
        glCreateFramebuffers(1, &_fbo);
        glCreateTextures(GL_TEXTURE_2D, 1, &_colorBuffer);
        glTextureStorage2D(_colorBuffer, 1, GL_RGBA8, renderTargetSize.x, renderTargetSize.y);
        glTextureParameteri(_colorBuffer, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTextureParameteri(_colorBuffer, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTextureParameteri(_colorBuffer, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTextureParameteri(_colorBuffer, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glNamedFramebufferTexture(_fbo, GL_COLOR_ATTACHMENT0, _colorBuffer, 0);
    };

    virtual void bindVrFramebuffer() {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, _fbo);
    }

    virtual void unbindVrFramebuffer() {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    }

    virtual void submitVrFrame() = 0;
    virtual void renderMirror() = 0;


    void render() {
        glfwMakeContextCurrent(window);
        // Tell the 
        gl::nv::vk::SignalSemaphore(vulkanRenderer.semaphores.renderStart);
        glFlush();
        vulkanRenderer.render();

        bindVrFramebuffer();
        gl::nv::vk::WaitSemaphore(vulkanRenderer.semaphores.renderComplete);
        gl::nv::vk::DrawVkImage(vulkanRenderer.framebuffer.colors[0].image, 0, vec2(0), renderTargetSize, 0, glm::vec2(0), glm::vec2(1));
        unbindVrFramebuffer();
        submitVrFrame();
        renderMirror();
        glfwSwapBuffers(window);
    }

    virtual void prepare() {
        // Make the on screen window 1/4 the resolution of the render target
        size = renderTargetSize;
        size /= 4;
        vulkanContext.createContext(false);
        vulkanRenderer.framebufferSize = renderTargetSize;
        vulkanRenderer.prepare();

        glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
        glfwWindowHint(GLFW_DEPTH_BITS, 16);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, true);
        window = glfwCreateWindow(size.x, size.y, "glfw", nullptr, nullptr);
        if (!window) {
            throw std::runtime_error("Unable to create rendering window");
        }
        glfwMakeContextCurrent(window);
        glfwSwapInterval(0);
        glewExperimental = true;
        glewInit();
        glGetError();
        gl::nv::vk::init();
        setupVrFramebuffer();
    }

    virtual void update(float delta) {
        vulkanRenderer.update(delta, eyeProjections, eyeViews);
    }

    virtual void run() final {
        prepare();
        auto tStart = std::chrono::high_resolution_clock::now();
        while (!glfwWindowShouldClose(window)) {
            auto tEnd = std::chrono::high_resolution_clock::now();
            auto tDiff = std::chrono::duration<double, std::milli>(tEnd - tStart).count();
            glfwPollEvents();
            update((float)tDiff / 1000.0f);
            render();
            ++frameCounter;
            fpsTimer += (float)tDiff;
            if (fpsTimer > 1000.0f) {
                std::string windowTitle = getWindowTitle();
                glfwSetWindowTitle(window, windowTitle.c_str());
                lastFPS = (float)frameCounter;
                fpsTimer = 0.0f;
                frameCounter = 0;
            }
            tStart = tEnd;
        }
    }

    virtual std::string getWindowTitle() {
        std::string device(vulkanContext.deviceProperties.deviceName);
        return "OpenGL Interop - " + device + " - " + std::to_string(frameCounter) + " fps";
    }
};
