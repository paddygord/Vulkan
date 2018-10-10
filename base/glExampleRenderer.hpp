#include "common.hpp"

#if !defined(__ANDROID__)

#include <glad/glad.h>

#define SHOW_GL_WINDOW 0

namespace gl {

class TextureGenerator {
public:
    using Lambda = std::function<void(GLuint)>;

    void create();
    void render(const glm::uvec2& newDimensions, GLuint target, const Lambda& preBlit = [](GLuint){}, const Lambda& postBlit = [](GLuint){});
    void destroy();

private:
    static const std::string VERTEX_SHADER;
    static const std::string FRAGMENT_SHADER;

    GLuint drawFbo{ 0 };
    GLuint blitFbo{ 0 };
    GLuint color{ 0 };
    GLuint vao{ 0 };
    GLuint program{ 0 };
    struct Locations {
        GLint rez{ 0 };
        GLint time{ 0 };
    } locations;
    double startTime{ 0.0 };
    glfw::Window window;
    glm::uvec2 dimensions{ 100, 100 };
};

}  // namespace gl

#endif
