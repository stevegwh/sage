//
// Created by Steve Wheeler on 23/08/2023.
//

#include "ObjParser.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <algorithm>
#include <regex>
#include <utility>
#include <glm/glm.hpp>
#include "constants.hpp"
#include "vendor/lodepng.h"
#include <unordered_map>

slib::texture DecodePng(const char* filename)
{
    
    std::vector<unsigned char> buffer;
    std::vector<unsigned char> image; //the raw pixels
    lodepng::load_file(buffer, filename);
    unsigned width, height;

    lodepng::State state;

    //decode
    unsigned error = lodepng::decode(image, width, height, state, buffer);
    const LodePNGColorMode& color = state.info_png.color;
    auto bpp = lodepng_get_bpp(&color);
    //if there's an error, display it
    if(error)
    {
        std::cout << "decoder error " << error << ": " << lodepng_error_text(error) << std::endl;
        exit(1);
    }

    //the pixels are now in the vector "image", 4 bytes per pixel, ordered RGBARGBA..., use it as texture, draw it, ...
    return { static_cast<int>(width), static_cast<int>(height), image,  4 };
}

std::string trim(const std::string& input)
{
    std::string output;
    // Remove excessive whitespace
    std::unique_copy (input.begin(), input.end(), std::back_insert_iterator<std::string>(output),
                      [](char a,char b){ return std::isspace(a) && std::isspace(b);});
    // Removes characters with ASCII value 0-31 (control characters)
    output.erase(std::remove_if(output.begin(), output.end(), [](char c) {
        return (c <= 31); 
    }), output.end());
    return output;
}

struct tri_obj
{
    const int v1, v2, v3;
    const int vt1, vt2, vt3;
    const int vn1, vn2, vn3;
    const std::string material;
};

std::array<float, 3> parseVectorLine(const std::string& input)
{
    std::stringstream ss(input);
    std::array<float,3> arr{};
    std::string token;
    int i = 0;
    while(std::getline(ss, token, ' '))
    {
        arr.at(i++) = std::stof(token);
    }
    return arr;
}

std::unordered_map<std::string, slib::material> parseMtlFile(const char* path)
{
    std::ifstream mtl(path);
    if (!mtl.is_open()) {
        std::cout << "Failed to open materials file" << std::endl;
        exit(1);
    }

    std::unordered_map<std::string, slib::material> toReturn;
    std::string materialKey;
    std::string line;
    slib::material material{};

    while (getline(mtl, line))
    {
        line = trim(line);
        if (line[0] == '#') continue;
        if (line.find("newmtl") != std::string::npos)
        {
            if (!materialKey.empty()) // Store the previous material
            {
                toReturn.insert({ materialKey, material });
                material = {};
            }
            materialKey = line.substr(line.find("newmtl") + std::string("newmtl ").length());
        }
        else if (line.find("map_Kd") != std::string::npos)
        {
            std::string mtlPath = line.substr(line.find("map_Kd") + std::string("map_Kd ").length());
            material.map_Kd = DecodePng(std::string(RES_PATH + mtlPath).c_str());;
        }
        else if (line.find("map_Ks") != std::string::npos)
        {
            std::string mtlPath = line.substr(line.find("map_Ks") + std::string("map_Ks ").length());
            material.map_Ks = DecodePng(std::string(RES_PATH + mtlPath).c_str());;
        }
        else if (line.find("map_Ns") != std::string::npos)
        {
            std::string mtlPath = line.substr(line.find("map_Ns") + std::string("map_Ns ").length());
            material.map_Ns = DecodePng(std::string(RES_PATH + mtlPath).c_str());;
        }
        else if (line.find("map_Disp") != std::string::npos)
        {
        }
        else if (line.find("map_Ka") != std::string::npos)
        {
        }
        else if (line.find("map_d") != std::string::npos)
        {
        }
        else if (line.find("Ka") != std::string::npos)
        {
            std::string input = line.substr(line.find("Ka ") + std::string("Ka ").length());
            std::array<float, 3> arr = parseVectorLine(input);
            material.Ka[0] = arr.at(0);
            material.Ka[1] = arr.at(1);
            material.Ka[2] = arr.at(2);
        }
        else if (line.find("Kd") != std::string::npos)
        {
            std::string input = line.substr(line.find("Kd ") + std::string("Kd ").length());
            std::array<float, 3> arr = parseVectorLine(input);
            material.Kd[0] = arr.at(0);
            material.Kd[1] = arr.at(1);
            material.Kd[2] = arr.at(2);
        }
        else if (line.find("Ks") != std::string::npos)
        {
            std::string input = line.substr(line.find("Ks ") + std::string("Ks ").length());
            std::array<float, 3> arr = parseVectorLine(input);
            material.Ks[0] = arr.at(0);
            material.Ks[1] = arr.at(1);
            material.Ks[2] = arr.at(2);
        }
        else if (line.find("Ke") != std::string::npos)
        {
            std::string input = line.substr(line.find("Ke ") + std::string("Ke ").length());
            std::array<float, 3> arr = parseVectorLine(input);
            material.Ke[0] = arr.at(0);
            material.Ke[1] = arr.at(1);
            material.Ke[2] = arr.at(2);
        }
        else if (line.find("Ni") != std::string::npos)
        {
            material.Ni = std::stof(line.substr(line.find("Ni ") + std::string("Ni ").length()));
        }
        else if (line.find("Ns") != std::string::npos)
        {
            material.Ns = std::stof(line.substr(line.find("Ns ") + std::string("Ns ").length()));
        }
        else if (line.find('d') != std::string::npos)
        {
            material.d = std::stof(line.substr(line.find("d ") + std::string("d ").length()));
        }
        else if (line.find("illum") != std::string::npos)
        {
            material.illum = std::stoi(line.substr(line.find("illum ") + std::string("illum ").length()));
        }
    }

    toReturn.insert({ materialKey, material });

    mtl.close();
    return toReturn;
}

glm::vec3 getVector(const std::string& line)
{
    std::array<float,3> arr = parseVectorLine(line);
    return { arr.at(0), arr.at(1), arr.at(2) };
}

glm::vec2 getTextureVector(const std::string& line)
{
    std::array<float,3> arr = parseVectorLine(line);
    return { arr.at(0), arr.at(1) };
}

tri_obj getFace(const std::string& line, std::string material)
{
    enum VertexFormat {
        V,
        V_VN,
        V_VT_VN,
        INVALID
    };
    
    std::string output = line;
    output.erase(0,2);
    std::stringstream ss(output);
    std::vector<int> vertices;
    std::vector<int> textureCoords;
    std::vector<int> normals;
    std::string token;

    std::regex v_pattern(R"(^\d+ \d+ \d+\s*$)");
    std::regex v_vn_pattern(R"(^\d+//\d+ \d+//\d+ \d+//\d+\s*$)");
    std::regex v_vt_vn_pattern(R"(^\d+/\d+/\d+ \d+/\d+/\d+ \d+/\d+/\d+\s*$)");

    VertexFormat format = INVALID;
    if (std::regex_match(output, v_pattern)) format = V;
    else if (std::regex_match(output, v_vn_pattern)) format = V_VN;
    else if (std::regex_match(output, v_vt_vn_pattern)) format = V_VT_VN;

    if (format == INVALID)
    {
        std::cout << "Error. Failed to parse faces in obj file." << '\n';
        std::cout << "Face string: " << output << std::endl;
        exit(1);
    }

    while(std::getline(ss, token, ' ')) 
    {
        switch (format) 
        {
        case V:
            vertices.push_back(std::stoi(token) - 1);
            break;
        case V_VN:
            vertices.push_back(std::stoi(token.substr(0, token.find("//"))) - 1);
            normals.push_back(std::stoi(token.substr(token.find("//") + 2)) - 1);
            break;
        case V_VT_VN:
            vertices.push_back(std::stoi(token.substr(0, token.find('/'))) - 1);
            token.erase(0, token.find('/') + 1);
            textureCoords.push_back(std::stoi(token.substr(0, token.find('/'))) - 1);
            token.erase(0, token.find('/') + 1);
            normals.push_back(std::stoi(token) - 1);
        }
    }

    switch (format)
    {
    case V:
        return {
            vertices.at(0), vertices.at(1), vertices.at(2),
            -1, -1, -1,
            -1, -1, -1,
            ""
        };
    case V_VN:
        return {
            vertices.at(0), vertices.at(1), vertices.at(2),
            -1, -1, -1,
            normals.at(0), normals.at(1), normals.at(2),
            ""
        };
    case V_VT_VN:
        return {
            vertices.at(0), vertices.at(1), vertices.at(2),
            textureCoords.at(0), textureCoords.at(1), textureCoords.at(2),
            normals.at(0), normals.at(1), normals.at(2),
            std::move(material)
        };
    }

}

namespace ObjParser
{
sage::Mesh ParseObj(const char* objPath)
{
    std::ifstream obj(objPath);
    if (!obj.is_open()) {
        std::cout << "Failed to open file" << std::endl;
        exit(1);
    }
    
    std::unordered_map<std::string, slib::material> materials;
    std::string currentTextureName;
    std::vector<glm::vec3> vertices;
    std::vector<glm::vec3> normals; // The normals as listed in the obj file
    std::vector<glm::vec2> textureCoords;
    std::vector<tri_obj> rawfaces; // faces in obj data (indices to arrays: v/vt/vn)
    std::vector<slib::tri> faces; // faces with data written directly (no separate arrays needed)
    std::string line;
    
    while (getline(obj, line))
    {
        line = trim(line);
        if (line.size() < 2) continue;
        if (line[0] == '#') continue;

        if (line.substr(0, 2) == "v ")
        {
            vertices.push_back(getVector(line.erase(0,2)));
        }
        else if (line.substr(0, 2) == "f ")
        {
            rawfaces.push_back(getFace(line, currentTextureName));
        }
        else if (line.substr(0, 2) == "vt")
        {
            textureCoords.push_back(getTextureVector(line.erase(0, 3)));
        }
        else if (line.substr(0, 2) == "vn")
        {
            normals.push_back(getVector(line.erase(0,3)));
        }
        else if (line.find("usemtl") != std::string::npos)
        {
            currentTextureName = line.substr(line.find("usemtl") + std::string("usemtl ").length());
        }
        else if (line.find("mtllib") != std::string::npos)
        {
            auto path = std::string (RES_PATH + line.substr(line.find("mtllib") + std::string("mtllib ").length()));
            materials = parseMtlFile(path.c_str());
        }
        
    }
    
    faces.reserve(rawfaces.size());
//#pragma omp parallel for default(none) shared(vertices, normals, textureCoords, rawfaces, faces)
    for (const tri_obj &tri : rawfaces)
    {
        slib::tri tri_n{};
        slib::vertex vx1{};
        slib::vertex vx2{};
        slib::vertex vx3{};
        vx1.position = vertices[tri.v1];
        vx2.position = vertices[tri.v2];
        vx3.position = vertices[tri.v3];
        vx1.normal = normals[tri.vn1];
        vx2.normal = normals[tri.vn2];
        vx3.normal = normals[tri.vn3];
        vx1.textureCoords = textureCoords[tri.vt1];
        vx2.textureCoords = textureCoords[tri.vt2];
        vx3.textureCoords = textureCoords[tri.vt3];
        tri_n.material = tri.material;
        tri_n.v1 = vx1;
        tri_n.v2 = vx2;
        tri_n.v3 = vx3;
        faces.push_back(tri_n);
    }
//#pragma omp barrier

    obj.close();
    std::vector<unsigned int> tmp;
    return { faces, materials, tmp };
}

std::vector<slib::texture> loadMaterialTextures(aiMaterial *mat, aiTextureType type, std::string typeName)
{
    std::vector<slib::texture> textures;
    for(unsigned int i = 0; i < mat->GetTextureCount(type); i++)
    {
        aiString str;
        mat->GetTexture(type, i, &str);
        std::string path(RES_PATH);
        slib::texture texture = DecodePng((path + str.C_Str()).c_str());
        //texture.id = TextureFromFile(str.C_Str(), directory);
        texture.type = typeName;
        texture.path = str.C_Str();
        textures.push_back(texture);
    }
    return textures;
}

sage::Mesh processMesh(aiMesh *mesh, const aiScene *scene)
{
    std::vector<slib::tri> faces;
    std::vector<unsigned int> indices;
    std::unordered_map<std::string, slib::material> materials;
    std::string faceMaterial;
    
    // process material
    if(mesh->mMaterialIndex >= 0)
    {
        if(mesh->mMaterialIndex >= 0)
        {
            aiMaterial *material = scene->mMaterials[mesh->mMaterialIndex];
            faceMaterial = scene->mMaterials[mesh->mMaterialIndex]->GetName().C_Str(); // TODO: Returns "", not "Texture1"

            std::vector<slib::texture> diffuseMaps = loadMaterialTextures(material,
                                                                          aiTextureType_DIFFUSE, "texture_diffuse");
            for (auto& tx: diffuseMaps)
            {
                materials[faceMaterial].map_Kd = tx;
            }
            //textures.insert(textures.end(), diffuseMaps.begin(), diffuseMaps.end());
            std::vector<slib::texture> specularMaps = loadMaterialTextures(material,
                                                                           aiTextureType_SPECULAR, "texture_specular");
            for (auto& tx: specularMaps)
            {
                materials[faceMaterial].map_Ks = tx;
            }

            //textures.insert(textures.end(), specularMaps.begin(), specularMaps.end());
        }
    }

    // process faces
    for(unsigned int i = 0; i < mesh->mNumFaces; i++)
    {
        slib::tri f;
        std::vector<slib::vertex> vertices;
        aiFace face = mesh->mFaces[i];

        for(unsigned int j = 0; j < 3; j++)
        {
            slib::vertex vertex{};
            glm::vec3 vector;
            vector.x = mesh->mVertices[face.mIndices[j]].x;
            vector.y = mesh->mVertices[face.mIndices[j]].y;
            vector.z = mesh->mVertices[face.mIndices[j]].z;
            vertex.position = vector;

            vector.x = mesh->mNormals[face.mIndices[j]].x;
            vector.y = mesh->mNormals[face.mIndices[j]].y;
            vector.z = mesh->mNormals[face.mIndices[j]].z;
            vertex.normal = vector;

            if(mesh->mTextureCoords[0]) // does the mesh contain texture coordinates?
            {
                glm::vec2 vec;
                vec.x = mesh->mTextureCoords[0][face.mIndices[j]].x;
                vec.y = mesh->mTextureCoords[0][face.mIndices[j]].y;
                vertex.textureCoords = vec;
            }
            else
                vertex.textureCoords = glm::vec2(0.0f, 0.0f);
            
            vertices.push_back(vertex);
        }
        f.v1 = vertices.at(0);
        f.v2 = vertices.at(1);
        f.v3 = vertices.at(2);
        f.material = faceMaterial;
        faces.push_back(f);
    }
    
    
    return { faces, materials, indices };
}

void processNode(aiNode *node, const aiScene *scene, std::vector<sage::Mesh>& meshes)
{
    // process all the node's meshes (if any)
    for(unsigned int i = 0; i < node->mNumMeshes; i++)
    {
        aiMesh *mesh = scene->mMeshes[node->mMeshes[i]];
        meshes.push_back(processMesh(mesh, scene));
    }
    // then do the same for each of its children
    for(unsigned int i = 0; i < node->mNumChildren; i++)
    {
        processNode(node->mChildren[i], scene, meshes);
    }
}

sage::Model LoadModel(const std::string& path)
{
    sage::Model model;
    Assimp::Importer import;
    const aiScene *scene = import.ReadFile(path, aiProcess_Triangulate | aiProcess_FlipUVs);

    if(!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode)
    {
        std::cout << "ERROR::ASSIMP::" << import.GetErrorString() << std::endl;
        return {};
    }
    //directory = path.substr(0, path.find_last_of('/'));
    processNode(scene->mRootNode, scene, model.meshes);
    return model;
}
}