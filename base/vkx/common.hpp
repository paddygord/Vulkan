#pragma once

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <ctime>

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

#include <glm/glm.hpp>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/noise.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <glm/gtx/rotate_vector.hpp>
#include <glm/gtx/transform.hpp>
#include <glm/gtx/quaternion.hpp>

// Image loading
#include <gli/gli.hpp>

// Vulkan!
#include <vulkan/vulkan.hpp>

using glm::ivec2;
using glm::mat3;
using glm::mat4;
using glm::quat;
using glm::uvec2;
using glm::vec2;
using glm::vec3;
using glm::vec4;

namespace Constants {
    constexpr float PHI = 1.61803398874f;

}

namespace Rotations {
    constexpr quat IDENTITY{ 1.0f, 0.0f, 0.0f, 0.0f };
    constexpr quat Y_180{ 0.0f, 0.0f, 1.0f, 0.0f };

    //  Helper function returns the positive angle (in radians) between two 3D vectors
    static inline float angleBetween(const glm::vec3& v1, const glm::vec3& v2) { return acosf((glm::dot(v1, v2)) / (glm::length(v1) * glm::length(v2))); }
};

namespace Vectors {
    static inline float lengthSquared(const vec3& v) {
        return glm::dot(v, v);
    }

    constexpr vec3 UNIT_X{ 1.0f, 0.0f, 0.0f };
    constexpr vec3 UNIT_Y{ 0.0f, 1.0f, 0.0f };
    constexpr vec3 UNIT_Z{ 0.0f, 0.0f, 1.0f };
    constexpr vec3 UNIT_NEG_X{ -1.0f, 0.0f, 0.0f };
    constexpr vec3 UNIT_NEG_Y{ 0.0f, -1.0f, 0.0f };
    constexpr vec3 UNIT_NEG_Z{ 0.0f, 0.0f, -1.0f };
// const vec3 Vectors::UNIT_XY{ glm::normalize(UNIT_X + UNIT_Y) };
// const vec3 Vectors::UNIT_XZ{ glm::normalize(UNIT_X + UNIT_Z) };
// const vec3 Vectors::UNIT_YZ{ glm::normalize(UNIT_Y + UNIT_Z) };
// const vec3 Vectors::UNIT_XYZ{ glm::normalize(UNIT_X + UNIT_Y + UNIT_Z) };
    //constexpr vec3 UNIT_XY;
    //constexpr vec3 UNIT_XZ;
    //constexpr vec3 UNIT_YZ;
    //constexpr vec3 UNIT_XYZ;
    constexpr vec3 MAX{ FLT_MAX };
    constexpr vec3 MIN{ -FLT_MAX };
    constexpr vec3 ZERO{ 0.0f };
    constexpr vec3 ONE{ 1.0f };
    constexpr vec3 TWO{ 2.0f };
    constexpr vec3 HALF{ 0.5f };
    constexpr vec3 RIGHT{ 1.0f, 0.0f, 0.0f };
    constexpr vec3 UP{ 0.0f, 1.0f, 0.0f };
    constexpr vec3 FRONT{ 0.0f, 0.0f, -1.0f };
    constexpr vec3 ZERO4;
};

#include "keycodes.hpp"
#include "glfw.hpp"

// Boilerplate for running an example
#if defined(__ANDROID__)
#define ENTRY_POINT_START                   \
    void android_main(android_app* state) { \
        vkx::android::androidApp = state;

#define ENTRY_POINT_END }
#else
#define ENTRY_POINT_START int main(const int argc, const char* argv[]) {
#define ENTRY_POINT_END \
    return 0;           \
    }
#endif

#define RUN_EXAMPLE(ExampleType) \
    ENTRY_POINT_START            \
    ExampleType().run();         \
    ENTRY_POINT_END

#define VULKAN_EXAMPLE_MAIN() RUN_EXAMPLE(VulkanExample)
