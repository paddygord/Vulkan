#version 460 core
#extension GL_EXT_nonuniform_qualifier : enable

const vec4 iMouse = vec4(0.0);

layout(location = 0) out vec4 outColor;

layout(std140, binding= 1) uniform paramsBuffer {
    vec4 params;
};

#define iResolution params.xy
#define iTime params.w
#define textureIndex params.z

#define SPARSE_2D_ARRAYS
//#define BINDLESS

#ifdef SPARSE_2D_ARRAYS
layout(binding = 0) uniform sampler2DArray textures;
#endif

#ifdef BINDLESS
layout(std140, binding = 0) uniform bindlessTextureBuffer {
    sampler2D textures[]
}
#endif


vec3 hash3(vec2 p) {
    vec3 q = vec3(dot(p, vec2(127.1, 311.7)), dot(p, vec2(269.5, 183.3)), dot(p, vec2(419.2, 371.9)));
    return fract(sin(q) * 43758.5453);
}

float iqnoise(in vec2 x, float u, float v) {
    vec2 p = floor(x);
    vec2 f = fract(x);

    float k = 1.0 + 63.0 * pow(1.0 - v, 4.0);

    float va = 0.0;
    float wt = 0.0;
    for (int j = -2; j <= 2; j++)
        for (int i = -2; i <= 2; i++) {
            vec2 g = vec2(float(i), float(j));
            vec3 o = hash3(p + g) * vec3(u, u, 1.0);
            vec2 r = g - f + o.xy;
            float d = dot(r, r);
            float ww = pow(1.0 - smoothstep(0.0, 1.414, sqrt(d)), k);
            va += o.z * ww;
            wt += ww;
        }

    return va / wt;
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = fragCoord.xy / iResolution.xx;

    vec2 p = 0.5 - 0.5 * sin(iTime * vec2(1.01, 1.71));

    if (iMouse.w > 0.001)
        p = vec2(0.0, 1.0) + vec2(1.0, -1.0) * iMouse.xy / iResolution;

    p = p * p * (3.0 - 2.0 * p);
    p = p * p * (3.0 - 2.0 * p);
    p = p * p * (3.0 - 2.0 * p);

    float f = iqnoise(24.0 * uv, p.x, p.y);

    fragColor = vec4(1.0);
    if (uv.x < 0.5) {
        fragColor.rgb = texture(textures, vec3(uv, textureIndex)).rgb;
    }
    fragColor.rgb *= f;
}

void main() {
    mainImage(outColor, gl_FragCoord.xy);
}
