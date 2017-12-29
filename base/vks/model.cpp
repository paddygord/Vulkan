/*
* Vulkan Model loader using ASSIMP
*
* Copyright(C) 2016-2017 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license(MIT) (http://opensource.org/licenses/MIT)
*/

#pragma once

#include "model.hpp"
#include "filesystem.hpp"

#include <assimp/Importer.hpp> 
#include <assimp/scene.h>     
#include <assimp/postprocess.h>
#include <assimp/cimport.h>
#include <assimp/material.h>

using namespace vks;
using namespace vks::model;

const int Model::defaultFlags = aiProcess_FlipWindingOrder | aiProcess_Triangulate | aiProcess_PreTransformVertices | aiProcess_CalcTangentSpace | aiProcess_GenSmoothNormals;

bool Model::loadFromFile(const Context& context, const std::string& filename, const VertexLayout& layout, vk::Optional<ModelCreateInfo> createInfo, const int flags) {
    device = context.device;

    Assimp::Importer Importer;
    const aiScene* pScene;

    // Load file
    vks::util::withBinaryFileContexts(filename, [&](size_t size, const void* data) {
        pScene = Importer.ReadFileFromMemory(data, size, flags);
    });

    if (!pScene) {
        std::string error = Importer.GetErrorString();
        throw std::runtime_error(error + "\n\nThe file may be part of the additional asset pack.\n\nRun \"download_assets.py\" in the repository root to download the latest version.");
    }

    parts.clear();
    parts.resize(pScene->mNumMeshes);

    glm::vec3 scale(1.0f);
    glm::vec2 uvscale(1.0f);
    glm::vec3 center(0.0f);
    if (createInfo.operator bool())
    {
        scale = createInfo->scale;
        uvscale = createInfo->uvscale;
        center = createInfo->center;
    }

    std::vector<float> vertexBuffer;
    std::vector<uint32_t> indexBuffer;

    vertexCount = 0;
    indexCount = 0;

    // Load meshes
    for (unsigned int i = 0; i < pScene->mNumMeshes; i++)
    {
        const aiMesh* paiMesh = pScene->mMeshes[i];

        parts[i] = {};
        parts[i].vertexBase = vertexCount;
        parts[i].indexBase = indexCount;

        vertexCount += pScene->mMeshes[i]->mNumVertices;

        aiColor3D pColor(0.f, 0.f, 0.f);
        pScene->mMaterials[paiMesh->mMaterialIndex]->Get(AI_MATKEY_COLOR_DIFFUSE, pColor);

        const aiVector3D Zero3D(0.0f, 0.0f, 0.0f);

        for (unsigned int j = 0; j < paiMesh->mNumVertices; j++)
        {
            const aiVector3D* pPos = &(paiMesh->mVertices[j]);
            const aiVector3D* pNormal = &(paiMesh->mNormals[j]);
            const aiVector3D* pTexCoord = (paiMesh->HasTextureCoords(0)) ? &(paiMesh->mTextureCoords[0][j]) : &Zero3D;
            const aiVector3D* pTangent = (paiMesh->HasTangentsAndBitangents()) ? &(paiMesh->mTangents[j]) : &Zero3D;
            const aiVector3D* pBiTangent = (paiMesh->HasTangentsAndBitangents()) ? &(paiMesh->mBitangents[j]) : &Zero3D;

            for (auto& component : layout.components)
            {
                switch (component) {
                case VERTEX_COMPONENT_POSITION:
                    vertexBuffer.push_back(pPos->x * scale.x + center.x);
                    vertexBuffer.push_back(-pPos->y * scale.y + center.y);
                    vertexBuffer.push_back(pPos->z * scale.z + center.z);
                    break;
                case VERTEX_COMPONENT_NORMAL:
                    vertexBuffer.push_back(pNormal->x);
                    vertexBuffer.push_back(-pNormal->y);
                    vertexBuffer.push_back(pNormal->z);
                    break;
                case VERTEX_COMPONENT_UV:
                    vertexBuffer.push_back(pTexCoord->x * uvscale.s);
                    vertexBuffer.push_back(pTexCoord->y * uvscale.t);
                    break;
                case VERTEX_COMPONENT_COLOR:
                    vertexBuffer.push_back(pColor.r);
                    vertexBuffer.push_back(pColor.g);
                    vertexBuffer.push_back(pColor.b);
                    break;
                case VERTEX_COMPONENT_TANGENT:
                    vertexBuffer.push_back(pTangent->x);
                    vertexBuffer.push_back(pTangent->y);
                    vertexBuffer.push_back(pTangent->z);
                    break;
                case VERTEX_COMPONENT_BITANGENT:
                    vertexBuffer.push_back(pBiTangent->x);
                    vertexBuffer.push_back(pBiTangent->y);
                    vertexBuffer.push_back(pBiTangent->z);
                    break;
                    // Dummy components for padding
                case VERTEX_COMPONENT_DUMMY_FLOAT:
                    vertexBuffer.push_back(0.0f);
                    break;
                case VERTEX_COMPONENT_DUMMY_VEC4:
                    vertexBuffer.push_back(0.0f);
                    vertexBuffer.push_back(0.0f);
                    vertexBuffer.push_back(0.0f);
                    vertexBuffer.push_back(0.0f);
                    break;
                };
            }

            dim.max.x = fmax(pPos->x, dim.max.x);
            dim.max.y = fmax(pPos->y, dim.max.y);
            dim.max.z = fmax(pPos->z, dim.max.z);

            dim.min.x = fmin(pPos->x, dim.min.x);
            dim.min.y = fmin(pPos->y, dim.min.y);
            dim.min.z = fmin(pPos->z, dim.min.z);
        }

        dim.size = dim.max - dim.min;

        parts[i].vertexCount = paiMesh->mNumVertices;

        uint32_t indexBase = static_cast<uint32_t>(indexBuffer.size());
        for (unsigned int j = 0; j < paiMesh->mNumFaces; j++)
        {
            const aiFace& Face = paiMesh->mFaces[j];
            if (Face.mNumIndices != 3)
                continue;
            indexBuffer.push_back(indexBase + Face.mIndices[0]);
            indexBuffer.push_back(indexBase + Face.mIndices[1]);
            indexBuffer.push_back(indexBase + Face.mIndices[2]);
            parts[i].indexCount += 3;
            indexCount += 3;
        }
    }

    // Vertex buffer
    vertices = context.stageToDeviceBuffer(vk::BufferUsageFlagBits::eVertexBuffer, vertexBuffer);
    // Index buffer
    indices = context.stageToDeviceBuffer(vk::BufferUsageFlagBits::eIndexBuffer, indexBuffer);
    return true;
};

bool Model::loadFromFile(const Context& context, const std::string& filename, VertexLayout layout, float scale, int flags) {
    return loadFromFile(context, filename, layout, ModelCreateInfo{ scale, 1.0f, 0.0f }, flags);
}
