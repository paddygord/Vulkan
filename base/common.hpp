#pragma once

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <iostream>
#include <iomanip>
#include <list>
#include <memory>
#include <mutex>
#include <queue>
#include <random>
#include <set>
#include <string>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <streambuf>
#include <thread>
#include <vector>

#if defined(__ANDROID__)
#include <android/native_activity.h>
#include <android/asset_manager.h>
#include <android_native_app_glue.h>
#endif

#include <glm/glm.hpp>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/noise.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <glm/gtx/rotate_vector.hpp>
#include <glm/gtx/transform.hpp>
#include <glm/gtx/quaternion.hpp>

using glm::ivec2;
using glm::uvec2;
using glm::vec2;
using glm::vec3;
using glm::vec4;
using glm::mat3;
using glm::mat4;
using glm::quat;

class Rotations {
 public:
    static const quat IDENTITY;
    static const quat Y_180;

    //  Helper function returns the positive angle (in radians) between two 3D vectors
    static inline float angleBetween(const glm::vec3& v1, const glm::vec3& v2) {
        return acosf((glm::dot(v1, v2)) / (glm::length(v1) * glm::length(v2)));
    }
};

class Vectors {
public:
    static const vec3 UNIT_X;
    static const vec3 UNIT_Y;
    static const vec3 UNIT_Z;
    static const vec3 UNIT_NEG_X;
    static const vec3 UNIT_NEG_Y;
    static const vec3 UNIT_NEG_Z;
    static const vec3 UNIT_XY;
    static const vec3 UNIT_XZ;
    static const vec3 UNIT_YZ;
    static const vec3 UNIT_XYZ;
    static const vec3 MAX;
    static const vec3 MIN;
    static const vec3 ZERO;
    static const vec3 ONE;
    static const vec3 TWO;
    static const vec3 HALF;
    static const vec3& RIGHT;
    static const vec3& UP;
    static const vec3& FRONT;
    static const vec3 ZERO4;
};

// Image loading
#include <gli/gli.hpp>

// Vulkan!
#include <vulkan/vulkan.hpp>

#if defined(__ANDROID__)
#include <android/keycodes.h>

extern android_app* global_android_app;
#define GLFW_KEY_F1 AKEYCODE_F1
#define GLFW_KEY_ESCAPE AKEYCODE_ESCAPE
#define GLFW_KEY_KP_ADD AKEYCODE_NUMPAD_ADD
#define GLFW_KEY_KP_SUBTRACT AKEYCODE_NUMPAD_SUBTRACT
#define GLFW_KEY_SPACE AKEYCODE_SPACE
#define GLFW_KEY_A AKEYCODE_A
#define GLFW_KEY_B AKEYCODE_B
#define GLFW_KEY_D AKEYCODE_D
#define GLFW_KEY_F AKEYCODE_F
#define GLFW_KEY_G AKEYCODE_G
#define GLFW_KEY_L AKEYCODE_L
#define GLFW_KEY_O AKEYCODE_O
#define GLFW_KEY_P AKEYCODE_P
#define GLFW_KEY_R AKEYCODE_R
#define GLFW_KEY_S AKEYCODE_S
#define GLFW_KEY_T AKEYCODE_T
#define GLFW_KEY_W AKEYCODE_W


#else
// Cross platform window management (except android)
#include "glfw.hpp"
#endif

// Boilerplate for running an example
#if defined(__ANDROID__)
#define ENTRY_POINT_START \
        void android_main(android_app* state) { \
            global_android_app = state;

#define ENTRY_POINT_END \
        }
#else
#define ENTRY_POINT_START \
        int main(const int argc, const char *argv[]) {

#define ENTRY_POINT_END \
            return 0; \
        }
#endif

#define RUN_EXAMPLE(ExampleType) \
    ENTRY_POINT_START \
        ExampleType* example = new ExampleType(); \
        example->run(); \
        delete(example); \
    ENTRY_POINT_END
