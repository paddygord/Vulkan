#include "common.hpp"
#include "vulkanShapes.hpp"
#include "vulkanGL.hpp"

#define VERTEX_BUFFER_BIND_ID 0
#define INSTANCE_BUFFER_BIND_ID 1

class OpenGLInteropExample {
public:
    vkx::Context vulkanContext;
    vkx::ShapesRenderer vulkanRenderer;
    GLFWwindow* window{ nullptr };
    glm::uvec2 size{ 1280, 720 };
    float fpsTimer{ 0 };
    float lastFPS{ 0 };
    uint32_t frameCounter{ 0 };

    OpenGLInteropExample() : vulkanRenderer{ vulkanContext } {
        glfwInit();
        vulkanContext.createContext(false);
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
    }

    ~OpenGLInteropExample() {
        if (nullptr != window) {
            glfwDestroyWindow(window);
        }
        glfwTerminate();
    }

    void render() {
        glfwMakeContextCurrent(window);

        // Tell the 
        gl::nv::vk::SignalSemaphore(vulkanRenderer.semaphores.renderStart);
        glFlush();
        vulkanRenderer.render();
        glClearColor(0, 0.5f, 0.8f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        gl::nv::vk::WaitSemaphore(vulkanRenderer.semaphores.renderComplete);
        gl::nv::vk::DrawVkImage(vulkanRenderer.framebuffer.colors[0].image, 0, vec2(0), vec2(1280, 720));
        glfwSwapBuffers(window);
    }

    void prepare() {
        vulkanRenderer.framebuffer.size = size;
        vulkanRenderer.prepare();
    }

    void run() {
        prepare();
        while (!glfwWindowShouldClose(window)) {
            auto tStart = std::chrono::high_resolution_clock::now();
            glfwPollEvents();
            render();
            ++frameCounter;
            auto tEnd = std::chrono::high_resolution_clock::now();
            auto tDiff = std::chrono::duration<double, std::milli>(tEnd - tStart).count();
            fpsTimer += (float)tDiff;
            if (fpsTimer > 1000.0f) {
                std::string windowTitle = getWindowTitle();
                glfwSetWindowTitle(window, windowTitle.c_str());
                lastFPS = frameCounter;
                fpsTimer = 0.0f;
                frameCounter = 0;
            }

            glm::mat4 view = glm::translate(glm::mat4(), glm::vec3(0, 0, -2.5));
            glm::mat4 projection = glm::perspective(glm::radians(60.0f), (float)size.x / (float)size.y, 0.001f, 256.0f);
            vulkanRenderer.update((float)tDiff / 1000.0f, projection, view);
        }
    }

    std::string getWindowTitle() {
        std::string device(vulkanContext.deviceProperties.deviceName);
        return "OpenGL Interop - " + device + " - " + std::to_string(frameCounter) + " fps";
    }
};

RUN_EXAMPLE(OpenGLInteropExample)
