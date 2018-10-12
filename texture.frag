#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable
#extension GL_EXT_nonuniform_qualifier : enable

struct Camera {
    mat4 projection;
    mat4 invProjection;
};

struct Transform {
    mat4 model;
    mat4 invModel;
};

struct Material {
    vec4 baseColor;
    int albedoTextureIndex;
};

layout(std430, set = 0, binding = 0) buffer camerasBuffer {
    Camera cameras[];
};

layout(std430, set = 0, binding = 1) buffer transformsBuffer {
    Transform transforms[];
};

layout(std430, set = 0, binding = 2) buffer materialsBuffer {
    Material materials[];
};

// layout(std430, set = 0, binding = 2) buffer texturesBuffer {
//     vec4 textures[];
// };

layout(set = 0, binding = 3) uniform sampler2D textures[];

// layout(set = 0, binding = 3) uniform sampler2D textures[];

layout(set = 2, binding = 0) uniform transformIndicesBuffer {
    int cameraIndex;
    int transformIndex;
} ubo;

layout(set = 3, binding = 0) uniform materialIndicesBuffer {
    int materialIndex[];
} mat;

layout(location = 0) in vec2 inUV;
layout(location = 1) in float inLodBias;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec3 inViewVec;
layout(location = 4) in vec3 inLightVec;

layout(location = 0) out vec4 outFragColor;


void main() {
    Material m = materials[mat.materialIndex[0]];
    vec4 color = texture(textures[m.albedoTextureIndex], inUV, inLodBias);

    vec3 N = normalize(inNormal);
    vec3 L = normalize(inLightVec);
    vec3 V = normalize(inViewVec);
    vec3 R = reflect(-L, N);

    vec3 diffuse = max(dot(N, L), 0.0) * vec3(1.0);
    float specular = pow(max(dot(R, V), 0.0), 16.0) * color.a;

    outFragColor = vec4(diffuse * color.rgb + specular, 1.0);
}