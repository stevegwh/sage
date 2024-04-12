//
// Created by Steve Wheeler on 23/08/2023.
//
#pragma once
#include <utility>
#include <vector>
#include <unordered_map>
#include <glm/glm.hpp>
#include "slib.hpp"
#include <string>

namespace sage
{
struct Mesh
{
    const std::vector<slib::tri> faces;
    const std::unordered_map<std::string, slib::material> materials;
    std::vector<unsigned int> indices;
    bool atlas = false; // Does this mesh use a texture atlas (requires 'tiles' of a consistent size)
    int atlasTileSize = 32;
    Mesh(const std::vector<slib::tri>& _faces,
         const std::unordered_map<std::string, slib::material>&  _materials,
         const std::vector<unsigned int>& _indices) :
        faces(_faces),
        materials(_materials),
        indices(_indices)
    {
#ifdef SAGE_OPENGL
        setupMesh();
#endif
    }
private:
#ifdef SAGE_OPENGL
    //  render data
    unsigned int VAO, VBO, EBO;
    void setupMesh();
#endif
};
}

