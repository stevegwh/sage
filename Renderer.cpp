//
// Created by Steve Wheeler on 27/08/2023.
//

#include "Renderer.hpp"
#include "constants.hpp"
#include "Rasterizer.hpp"
#include <iostream>

namespace sage
{

inline void createScreenSpace(std::vector<slib::tri>& faces)
{
    // Convert to screen
#pragma omp parallel for default(none) shared(faces, SCREEN_WIDTH, SCREEN_HEIGHT)
    for (auto& f : faces)
    {
        auto& v1 = f.v1.projectedPoint;
        // NDC Space
        if (v1.w != 0)
        {
            // Perspective divide
            v1.x /= v1.w;
            v1.y /= v1.w;
            v1.z /= v1.w;
        }
        //-----------------------------

        // Screen space
        const auto x1 = static_cast<float>(SCREEN_WIDTH / 2 + v1.x * SCREEN_WIDTH / 2);
        const auto y1 = static_cast<float>(SCREEN_HEIGHT / 2 - v1.y * SCREEN_HEIGHT / 2);
        f.v1.screenPoint = {x1, y1, v1.z};
        //-----------------------------

        auto& v2 = f.v2.projectedPoint;
        // NDC Space
        if (v2.w != 0)
        {
            // Perspective divide
            v2.x /= v2.w;
            v2.y /= v2.w;
            v2.z /= v2.w;
        }
        //-----------------------------

        // Screen space
        const auto x2 = static_cast<float>(SCREEN_WIDTH / 2 + v2.x * SCREEN_WIDTH / 2);
        const auto y2 = static_cast<float>(SCREEN_HEIGHT / 2 - v2.y * SCREEN_HEIGHT / 2);
        f.v2.screenPoint = {x2, y2, v2.z};
        //-----------------------------

        auto& v3 = f.v3.projectedPoint;
        // NDC Space
        if (v3.w != 0)
        {
            // Perspective divide
            v3.x /= v3.w;
            v3.y /= v3.w;
            v3.z /= v3.w;
        }
        //-----------------------------

        // Screen space
        const auto x3 = static_cast<float>(SCREEN_WIDTH / 2 + v3.x * SCREEN_WIDTH / 2);
        const auto y3 = static_cast<float>(SCREEN_HEIGHT / 2 - v3.y * SCREEN_HEIGHT / 2);
        f.v3.screenPoint = {x3, y3, v3.z};
        //-----------------------------

    }
#pragma omp barrier
}

inline bool makeClipSpace(const slib::tri &f,
                          std::vector<slib::tri>& processedFaces)
{
    // count inside/outside points
    // if face is entirely in the frustum, push it to processedFaces.
    // if face is entirely outside the frustum, return.
    // if partially, clip triangle.
    // // if inside == 2, form a quad.
    // // if inside == 1, form triangle.

    const auto &v1 = f.v1.projectedPoint;
    const auto &v2 = f.v2.projectedPoint;
    const auto &v3 = f.v3.projectedPoint;

    if (v1.x > v1.w && v2.x > v2.w && v3.x > v3.w) return false;
    if (v1.x < -v1.w && v2.x < -v2.w && v3.x < -v3.w) return false;
    if (v1.y > v1.w && v2.y > v2.w && v3.y > v3.w) return false;
    if (v1.y < -v1.w && v2.y < -v2.w && v3.y < -v3.w) return false;
    if (v1.z < 0.0f && v2.z < 0.0f && v3.z < 0.0f) return false;
    // TODO: far plane clipping doesn't work?
    //if (v1.z > v1.w && v2.z > v2.w && v3.z > v3.w) return false;


    processedFaces.push_back(f);
    return true;
    // clip *all* triangles against 1 edge, then all against the next, and the next.
}

inline void Renderer::clearBuffer()
{
    auto *pixels = (unsigned char *) sdlSurface->pixels;
    #pragma omp parallel for default(none) shared(pixels)
    for (int i = 0; i < screenSize * 4; ++i) pixels[i] = 0;
    #pragma omp barrier
}

void Renderer::RenderBuffer()
{
    SDL_RenderPresent(sdlRenderer);
    clearBuffer();
}

inline void pushBuffer(SDL_Renderer* renderer, SDL_Surface* surface)
{
    SDL_Texture *tex = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_RenderCopy(renderer, tex, nullptr, nullptr);
    SDL_DestroyTexture(tex);
}

inline void Renderer::updateViewMatrix()
{
    viewMatrix = slib::fpsviewGl({ camera.pos.x, camera.pos.y, camera.pos.z }, camera.rotation.x, camera.rotation.y);
    // TODO: Replace this with an event that camera subscribes to
    camera.UpdateDirectionVectors(viewMatrix);
}

inline void createProjectedSpace(const Renderable& renderable, const glm::mat4& viewMatrix, const glm::mat4& perspectiveMat,
                                 std::vector<slib::tri>& faces)
{
    glm::mat4 scaleMatrixGl = glm::scale(glm::mat4(1.0f), { renderable.scale.x, renderable.scale.y, renderable.scale.z });
    glm::mat4 rotationMatrixX = glm::rotate(glm::mat4(1.0f), renderable.eulerAngles.x, { 1, 0, 0 });
    glm::mat4 rotationMatrixY = glm::rotate(glm::mat4(1.0f), renderable.eulerAngles.y, { 0, 1, 0 });
    glm::mat4 rotationMatrixZ = glm::rotate(glm::mat4(1.0f), renderable.eulerAngles.z, { 0, 0, 1 });
    glm::mat4 rotationMatrixGl= rotationMatrixZ * rotationMatrixY * rotationMatrixX;
    glm::mat4 translationMatrixGl = glm::translate(glm::mat4(1.0f), { renderable.position.x, renderable.position.y, renderable.position.z });
    
    // World Space Transform
    glm::mat4 normalTransformMat =  rotationMatrixGl * scaleMatrixGl; // Normal transforms do not need to be translated
    glm::mat4 fullTransformMat =  translationMatrixGl * normalTransformMat;
    auto viewTransform = viewMatrix * fullTransformMat;
    
#pragma omp parallel for default(none) shared(renderable, viewMatrix, perspectiveMat, faces, normalTransformMat, viewTransform)
    for (auto& f : faces)
    {
        f.v1.projectedPoint = viewTransform * glm::vec4(f.v1.position, 1) * perspectiveMat;
        f.v1.normal = normalTransformMat * glm::vec4(f.v1.normal, 0);
        f.v2.projectedPoint = viewTransform * glm::vec4(f.v2.position, 1) * perspectiveMat;
        f.v2.normal = normalTransformMat * glm::vec4(f.v2.normal, 0);
        f.v3.projectedPoint = viewTransform * glm::vec4(f.v3.position, 1) * perspectiveMat;
        f.v3.normal = normalTransformMat * glm::vec4(f.v3.normal, 0);
    }
#pragma omp barrier
}

void Renderer::Render()
{
    zBuffer->clear();
    updateViewMatrix();
    for (const auto& renderable : renderables) 
    {
        for (const auto& mesh : renderable->model.meshes) 
        {
            std::vector<slib::tri> faces = mesh.faces;
            std::vector<slib::tri> processedFaces;
            processedFaces.reserve(faces.size());

            createProjectedSpace(*renderable, viewMatrix, perspectiveMat, faces);

            // Culling and clipping
            for (const auto &f : faces)
            {
                makeClipSpace(f, processedFaces);
            }
            createScreenSpace(processedFaces);

            #pragma omp parallel for default(none) shared(processedFaces, mesh)
            for (const auto &f : processedFaces)
            {
                const auto &p1 = f.v1.screenPoint;
                const auto &p2 = f.v2.screenPoint;
                const auto &p3 = f.v3.screenPoint;

                const float area = (p3.x - p1.x) * (p2.y - p1.y) - (p3.y - p1.y) * (p2.x - p1.x); // area of the triangle multiplied by 2
                if (area < 0) continue; // Backface culling
                Rasterizer rasterizer(zBuffer, mesh, f, sdlSurface, fragmentShader, textureFilter);
                rasterizer.rasterizeTriangle(area);
            }
        }
        pushBuffer(sdlRenderer, sdlSurface);
    }
}

void Renderer::AddRenderable(const Renderable* renderable)
{
    renderables.push_back(renderable);
}

void Renderer::ClearRenderables()
{
    renderables.clear();
}

void Renderer::setShader(FragmentShader shader)
{
//    if (shader == GOURAUD)
//    {
//        for (auto& renderable : renderables)
//        {
//            if (renderable->mesh.normals.empty())
//            {
//                std::cout << "Warning: renderable does not have vertex normals. Falling back to flat shading.";
//                fragmentShader = FLAT;
//                return;
//            }
//        }
//    }
    fragmentShader = shader;
}

void Renderer::setTextureFilter(sage::TextureFilter filter)
{
    textureFilter = filter;
}

}