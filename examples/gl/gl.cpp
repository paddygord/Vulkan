/*
* Vulkan Example - Instanced mesh rendering, uses a separate vertex buffer for instanced data
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/
#include <common.hpp>
#include <glfw/glfw.hpp>

static const std::string VERTEX_SHADER = R"SHADER(
#version 450 core
#line 11

const vec4 VERTICES[] = vec4[](
    vec4(-1.0, -1.0, 0.0, 1.0), 
    vec4( 1.0, -1.0, 0.0, 1.0),    
    vec4(-1.0,  1.0, 0.0, 1.0),
    vec4( 1.0,  1.0, 0.0, 1.0)
);   

layout(location = 0) out vec2 outFragCoord;

void main() {
    vec4 vertex = VERTICES[gl_VertexID];
    vec2 uv = vertex.xy;
    uv += 1.0;
    uv /= 2.0;
    gl_Position = vertex;
    outFragCoord = uv;
}

)SHADER";


static const std::string FRAGMENT_SHADER = R"SHADER(
#version 450 core

const vec4 iMouse = vec4(0.0); 
layout(location = 0) uniform vec3      iResolution;
layout(location = 1) uniform float     iTime;
layout(location = 0) in vec2 inFragCoord;
layout(location = 0) out vec4 outColor;

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

void main() {
    mainImage(outColor, gl_FragCoord.xy);
    //outColor = vec4(0.0, 1.0, 0.0, 1.0);
}


)SHADER";




static void debugMessageCallback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const void* userParam) {
    if (GL_DEBUG_SEVERITY_NOTIFICATION == severity) {
        return;
    }
    // FIXME For high severity errors, force a sync to the log, since we might crash
    // before the log file was flushed otherwise.  Performance hit here
    std::cout << "OpenGL: " << message;
}



GLuint loadShader(const std::string& shaderSource, GLenum shaderType) {
    GLuint shader = glCreateShader(shaderType);
    int sizes = shaderSource.size();
    const GLchar* strings = shaderSource.c_str();
    glShaderSource(shader, 1, &strings, &sizes);
    glCompileShader(shader);

    GLint isCompiled = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &isCompiled);
    if (isCompiled == GL_FALSE) {
        GLint maxLength = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &maxLength);

        // The maxLength includes the NULL character
        std::vector<GLchar> errorLog(maxLength);
        glGetShaderInfoLog(shader, maxLength, &maxLength, &errorLog[0]);
        std::string strError;
        strError.insert(strError.end(), errorLog.begin(), errorLog.end());


        // Provide the infolog in whatever manor you deem best.
        // Exit with failure.
        glDeleteShader(shader); // Don't leak the shader.
        throw std::runtime_error("Shader compiled failed");
    }
    return shader;
}

class GlInteropExample {
public:
    GLuint color = 0;
    GLuint fbo = 0;
    GLuint vao = 0;

    //GL_EXT_memory_object
    //    GL_EXT_memory_object_win32


    void run() {
        glfw::Window::init();
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, 1);
        glfw::Window  window;
        window.createWindow({ 512, 512 });
        window.makeCurrent();
        gl::init();
        static const time_t start = GetTickCount();

        {
            std::cout << glGetString(GL_VENDOR) << std::endl;
            std::cout << glGetString(GL_RENDERER) << std::endl;
            std::cout << glGetString(GL_VERSION) << std::endl;
            GLint n;
            glGetIntegerv(GL_NUM_EXTENSIONS, &n);
            if (n > 0) {
                GLint i;
                for (i = 0; i < n; i++) {
                    std::cout << "\t" << glGetStringi(GL_EXTENSIONS, i) << std::endl;
                }
            }
        }
        
        glDebugMessageCallback(debugMessageCallback, NULL);
        glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS_ARB);

        glDisable(GL_DEPTH_TEST);
        glClearColor(1, 0, 0, 1);

        glCreateTextures(GL_TEXTURE_2D, 1, &color);
        glTextureStorage2D(color, 1, GL_RGBA8, 512, 512);

        glCreateFramebuffers(1, &fbo);
        glNamedFramebufferTexture(fbo, GL_COLOR_ATTACHMENT0, color, 0);

        // Now check for completness
        auto fboStatus = glCheckNamedFramebufferStatus(fbo, GL_DRAW_FRAMEBUFFER);

        GLuint program = glCreateProgram();
        {
            GLuint vs = loadShader(VERTEX_SHADER, GL_VERTEX_SHADER);
            GLuint fs = loadShader(FRAGMENT_SHADER, GL_FRAGMENT_SHADER);
            glAttachShader(program, vs);
            glAttachShader(program, fs);
            glLinkProgram(program);
            glDeleteShader(vs);
            glDeleteShader(fs);
        }
        glGenVertexArrays(1, &vao);
        glBindVertexArray(vao);
        glUseProgram(program);
        glProgramUniform3f(program, 0, 512.0f, 512.0f, 0.0f);

        window.runWindowLoop([&] {
            float now = start - GetTickCount();
            glClearColor(1, 0, 0, 1);
            glClear(GL_COLOR_BUFFER_BIT);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo);
            glViewport(0, 0, 512, 512);
            glClearColor(1, 0, 1, 1);
            glClear(GL_COLOR_BUFFER_BIT);
            glProgramUniform1f(program, 1, now / 1000.0f);
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
            glBlitNamedFramebuffer(fbo, 0, 0, 0, 512, 512, 0, 0, 512, 512, GL_COLOR_BUFFER_BIT, GL_NEAREST);
            window.present();
        });
    }
};


RUN_EXAMPLE(GlInteropExample)
