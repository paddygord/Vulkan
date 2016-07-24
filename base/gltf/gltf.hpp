//
//  Created by Bradley Austin Davis on 2016/07/17
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#pragma once
#ifndef gltf_hpp
#define gltf_hpp

#include "forward.hpp"
#include <vector>
#include <string>
#include <unordered_map>

namespace gltf {
    namespace impl {
        class variant : public std::string {

        };

        using variant_map = std::unordered_map<std::string, variant>;

        class named_object {
        public:
            std::string name;
        };

        class extended_named_object : public named_object {
        public:
            variant_map extensions;
            variant extras;
        };
    }


    namespace scenes {
        class scene : public impl::extended_named_object {
            std::vector<node_ptr> nodes;
        };

        class node : public impl::named_object {
            camera_ptr camera;
            std::vector<node_ptr> children;
            std::vector<skins::skeleton_ptr> skeletons;
            skins::skin_ptr skin;
            std::string jointName;
            mat4 matrix;
            std::vector<meshes::mesh_ptr> meshes;
            quat rotation;
            vec3 scale;
            vec3 translation;
        };
    }

    namespace constants {
        enum class component {
            Byte = 5120,
            UnsignedByte = 5122,
            Short = 5123,
            Float = 5126,
        };

        enum class type {
            Scalar,
            Vec2,
            Vec3,
            Vec4,
            Mat2,
            Mat3,
            Mat4,
        };
    }


    namespace meshes {
        class mesh;
    }

    namespace buffers {
        class buffer;
        class view;

        class accessor : public impl::extended_named_object {
            view_ptr bufferView;
            size_t byteOffset;
            size_t byteStride{ 0 };
            constants::component componentType;
            size_t count;
            constants::type type;
            uint8_t max{ 0 };
            uint8_t min{ 0 };
        };
    }

    namespace shaders {
        class shader;
        class program;
    }

    namespace textures {
        class image;
    }

    namespace materials {
        class material;
    }

    namespace skins {

    }

}

#endif
