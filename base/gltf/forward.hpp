//
//  Created by Bradley Austin Davis on 2016/07/17
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#pragma once
#ifndef gltf_forward_hpp
#define gltf_forward_hpp

#include <memory>
#include <string>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace gltf {
    using glm::ivec2;
    using glm::uvec2;
    using glm::vec2;
    using glm::vec3;
    using glm::vec4;
    using glm::mat3;
    using glm::mat4;
    using glm::quat;

    class root;

    namespace scenes {
        class scene;
        using scene_ptr = std::shared_ptr<scene>;
        class node;
        using node_ptr = std::shared_ptr<node>;
        class camera;
        using camera_ptr = std::shared_ptr<camera>;
    }

    namespace meshes {
        class mesh;
        using mesh_ptr = std::shared_ptr<mesh>;
    }

    namespace buffers {
        class buffer;
        using buffer_ptr = std::shared_ptr<buffer>;
        class view;
        using view_ptr = std::shared_ptr<view>;
        class accessor;
        using accessor_ptr = std::shared_ptr<accessor>;
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
        class skin;
        using skin_ptr = std::shared_ptr<skin>;
        class skeleton;
        using skeleton_ptr = std::shared_ptr<skeleton>;
    }
}

#endif
