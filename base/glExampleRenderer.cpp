#include "glExampleRenderer.hpp"

static void glfwErrorCallback(int, const char* message) {
    std::cerr << message << std::endl;
}


const std::string gl::TextureGenerator::VERTEX_SHADER = R"SHADER(
#version 450 core

const vec4 VERTICES[] = vec4[](
    vec4(-1.0, -1.0, 0.0, 1.0), 
    vec4( 1.0, -1.0, 0.0, 1.0),    
    vec4(-1.0,  1.0, 0.0, 1.0),
    vec4( 1.0,  1.0, 0.0, 1.0)
);   

void main() { gl_Position = VERTICES[gl_VertexID]; }

)SHADER";

const std::string gl::TextureGenerator::FRAGMENT_SHADER = R"SHADER(
#version 450 core

const vec4 iMouse = vec4(0.0); 

layout(location = 0) out vec4 outColor;

layout(location = 0) uniform vec3 iResolution;
layout(location = 1) uniform float iTime;

vec3 hash3( vec2 p )
{
    vec3 q = vec3( dot(p,vec2(127.1,311.7)), 
                   dot(p,vec2(269.5,183.3)), 
                   dot(p,vec2(419.2,371.9)) );
    return fract(sin(q)*43758.5453);
}

float iqnoise( in vec2 x, float u, float v )
{
    vec2 p = floor(x);
    vec2 f = fract(x);
        
    float k = 1.0+63.0*pow(1.0-v,4.0);
    
    float va = 0.0;
    float wt = 0.0;
    for( int j=-2; j<=2; j++ )
    for( int i=-2; i<=2; i++ )
    {
        vec2 g = vec2( float(i),float(j) );
        vec3 o = hash3( p + g )*vec3(u,u,1.0);
        vec2 r = g - f + o.xy;
        float d = dot(r,r);
        float ww = pow( 1.0-smoothstep(0.0,1.414,sqrt(d)), k );
        va += o.z*ww;
        wt += ww;
    }
    
    return va/wt;
}

void mainImage( out vec4 fragColor, in vec2 fragCoord )
{
    vec2 uv = fragCoord.xy / iResolution.xx;

    vec2 p = 0.5 - 0.5*sin( iTime*vec2(1.01,1.71) );
    
    if( iMouse.w>0.001 ) p = vec2(0.0,1.0) + vec2(1.0,-1.0)*iMouse.xy/iResolution.xy;
    
    p = p*p*(3.0-2.0*p);
    p = p*p*(3.0-2.0*p);
    p = p*p*(3.0-2.0*p);
    
    float f = iqnoise( 24.0*uv, p.x, p.y );
    
    fragColor = vec4( f, f, f, 1.0 );
}

void main() { mainImage(outColor, gl_FragCoord.xy); }

)SHADER";

void gl::TextureGenerator::create() {
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

    // Window doesn't need to be large, it only exists to give us a GL context
    window.createWindow(dimensions);
    window.setTitle("OpenGL 4.1");
    window.makeCurrent();

    startTime = glfwGetTime();

    gl::init();
    gl::setupDebugLogging();
    glfwSetErrorCallback(glfwErrorCallback);

#if !SHOW_GL_WINDOW
    window.showWindow(false);
#endif
    // The remaining initialization code is all standard OpenGL
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    glGenFramebuffers(2, &drawFbo);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, drawFbo);
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    program = gl::buildProgram(VERTEX_SHADER, FRAGMENT_SHADER);
    locations.rez = glGetUniformLocation(program, "iResolution");
    locations.time = glGetUniformLocation(program, "iTime");
}

void gl::TextureGenerator::destroy() {
    glBindVertexArray(0);
    glUseProgram(0);
    glDeleteFramebuffers(2, &drawFbo);
    glDeleteVertexArrays(1, &vao);
    glDeleteProgram(program);
    glFlush();
    glFinish();
    window.destroyWindow();
}

void gl::TextureGenerator::render(const glm::uvec2& renderDimensions, GLuint targetTexture, const Lambda& preBlit, const Lambda& postBlit) {
    if (!color || dimensions != renderDimensions) {
        dimensions = renderDimensions;
        window.setSize(dimensions);
        if (color) {
            glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_RECTANGLE, 0, 0);
            glDeleteTextures(1, &color);
            color = 0;
        }
        glGenTextures(1, &color);
        glBindTexture(GL_TEXTURE_2D, color);
        glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, dimensions.x, dimensions.y);
        glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);
    }


    // Basic GL rendering code to render animated noise to a texture
    glUseProgram(program);
    // Draw to the draw framebuffer
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, drawFbo);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);

    glViewport(0, 0, dimensions.x, dimensions.y);
    glProgramUniform1f(program, locations.time, (float)(glfwGetTime() - startTime));
    glProgramUniform3f(program, locations.rez, (float)dimensions.x, (float)dimensions.y, 0.0f);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    // Prepare 
    preBlit(targetTexture);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, blitFbo);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, targetTexture, 0);
    glBlitFramebuffer(0, 0, dimensions.x, dimensions.y, 0, 0, dimensions.x, dimensions.y, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
    postBlit(targetTexture);


#if SHOW_GL_WINDOW
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glBlitFramebuffer(0, 0, dimensions.x, dimensions.y, 0, 0, dimensions.x, dimensions.y, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    window.present();
#else
    // When using synchronization across multiple GL context, or in this case
    // across OpenGL and another API, it's critical that an operation on a
    // synchronization object that will be waited on in another context or API
    // is flushed to the GL server.
    //
    // Failure to flush the operation can cause the GL driver to sit and wait for
    // sufficient additional commands in the buffer before it flushes automatically
    // but depending on how the waits and signals are structured, this may never
    // occur.
    glFlush();
#endif
}
