//
// Created by Steve Wheeler on 30/08/2023.
//

#pragma once

#include <utility>
#include "Model.hpp"

namespace sage
{
struct Renderable
{
    const Model model;
    glm::vec3 position;
    glm::vec3 eulerAngles;
    glm::vec3 scale;
    slib::Color col;
    bool ignoreLighting = false;
    Renderable(Model _model,
                glm::vec3 _position,
                glm::vec3 _eulerAngles,
                glm::vec3 _scale,
                slib::Color _col) : 
                model(std::move(_model)), 
                position(_position), 
                eulerAngles(_eulerAngles), 
                scale(_scale), 
                col(_col)
    {};

};
}