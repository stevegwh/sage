//
// Created by Steve Wheeler on 23/08/2023.
//

#pragma once
#include <vector>
#include <array>
#include "slib.hpp"
#include "Mesh.hpp"
#include "Model.hpp"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>


namespace ObjParser
{
    sage::Mesh ParseObj(const char* objPath);
    sage::Model LoadModel(const std::string& path);
};