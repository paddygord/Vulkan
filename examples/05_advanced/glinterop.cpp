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
    const float duration = 4.0f;
    const float interval = 6.0f;
    float zoom{ -1.0f };
    float rotationSpeed{ 0.25 };
    float zoomDelta{ 135 };
    float zoomStart{ 0 };
    float accumulator{ FLT_MAX };
    float frameTimer{ 0 };
    bool paused{ false };
    glm::quat orientation;

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

    void update(float deltaTime) {
        frameTimer = deltaTime;
        accumulator += frameTimer;
        if (accumulator < duration) {
            zoom = easings::inOutQuint(accumulator, duration, zoomStart, zoomDelta);
        } 

        if (accumulator >= interval) {
            accumulator = 0;
            zoomStart = zoom;
            if (zoom < -2) {
                zoomDelta = 135;
            } else {
                zoomDelta = -135;
            }
        }
    }

    void run() {
        prepare();
        auto tStart = std::chrono::high_resolution_clock::now();
        while (!glfwWindowShouldClose(window)) {
            auto tEnd = std::chrono::high_resolution_clock::now();
            auto tDiff = std::chrono::duration<float, std::milli>(tEnd - tStart).count();
            tStart = tEnd;

            update(tDiff / 1000.0f);

            auto projection = glm::perspective(glm::radians(60.0f), (float)size.x / (float)size.y, 0.001f, 256.0f);
            auto view = glm::translate(glm::mat4(), glm::vec3(0.0f, 0.0f, zoom)) * glm::mat4_cast(orientation);
            vulkanRenderer.update((float)tDiff / 1000.0f, projection, view);
            glfwPollEvents();
            render();
            ++frameCounter;
            fpsTimer += (float)tDiff;
            if (fpsTimer > 1000.0f) {
                std::string windowTitle = getWindowTitle();
                glfwSetWindowTitle(window, windowTitle.c_str());
                lastFPS = frameCounter;
                fpsTimer = 0.0f;
                frameCounter = 0;
            }
        }
    }

    std::string getWindowTitle() {
        std::string device(vulkanContext.deviceProperties.deviceName);
        return "OpenGL Interop - " + device + " - " + std::to_string(frameCounter) + " fps";
    }
};

RUN_EXAMPLE(OpenGLInteropExample)
