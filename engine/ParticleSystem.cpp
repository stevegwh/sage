#include "ParticleSystem.hpp"
#include "engine/MathConstants.hpp"

#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include <algorithm>
#include <utility>

/**
 * This is a heavily modified version of the 'libpartikel' project modified for 3D and ported to C++.
 * This project carries the implementation as a C++ source file, not as a header-only library.
 * The original notice is included below.
 */
/**********************************************************************************************
 *
 *   libpartikel v0.0.3 ALPHA
 *   [https://github.com/dbriemann/libpartikel]
 *
 *
 *   A simple particle system built with and for raylib, to be used as header
 *only library.
 *
 *
 *   FEATURES:
 *       - Supports all platforms that raylib supports
 *
 *   DEPENDENCIES:
 *       raylib >= v2.5.0 and all of its dependencies
 *
 *   CONFIGURATION:
 *   #define LIBPARTIKEL_IMPLEMENTATION
 *       Generates the implementation of the library into the included file.
 *       If not defined, the library is in header only mode and can be included
 *in other headers or source files without problems. But only ONE file should
 *hold the implementation.
 *
 *   LICENSE: zlib/libpng
 *
 *   libpartikel is licensed under an unmodified zlib/libpng license, which is
 *an OSI-certified, BSD-like license that allows static linking with closed
 *source software:
 *
 *   Copyright (c) 2017 David Linus Briemann (@Raging_Dave)
 *
 *   This software is provided "as-is", without any express or implied warranty.
 *In no event will the authors be held liable for any damages arising from the
 *use of this software.
 *
 *   Permission is granted to anyone to use this software for any purpose,
 *including commercial applications, and to alter it and redistribute it freely,
 *subject to the following restrictions:
 *
 *     1. The origin of this software must not be misrepresented; you must not
 *claim that you wrote the original software. If you use this software in a
 *product, an acknowledgment in the product documentation would be appreciated
 *but is not required.
 *
 *     2. Altered source versions must be plainly marked as such, and must not
 *be misrepresented as being the original software.
 *
 *     3. This notice may not be removed or altered from any source
 *distribution.
 *
 **********************************************************************************************/

namespace sage
{
    // Utility functions & structs.
    //----------------------------------------------------------------------------------
    namespace
    {
        float RandomFloat(std::mt19937& random, float min, float max)
        {
            return std::uniform_real_distribution<float>(std::min(min, max), std::max(min, max))(random);
        }
    } // namespace

    // Assuming these utility functions are available or need to be implemented for Vector3
    Vector3 RotateV3(const Vector3& vec, float angleX, float angleY, float angleZ)
    {
        // Implement or use existing functions to rotate a Vector3 around the X, Y, and Z axes
        Matrix rotationMatrix = MatrixRotateXYZ(Vector3{.x = angleX, .y = angleY, .z = angleZ});
        return Vector3Transform(vec, rotationMatrix);
    }

    bool Particle_DeactivatorAge(Particle* p)
    {
        return p->age > p->ttl;
    }

    // Particle constructor
    Particle::Particle(const std::function<bool(Particle*)>& deactivatorFunc)
        : origin({.x = 0, .y = 0, .z = 0}),
          position({.x = 0, .y = 0, .z = 0}),
          velocity({.x = 0, .y = 0, .z = 0}),
          externalAcceleration({.x = 0, .y = 0, .z = 0}),
          particle_Deactivator(deactivatorFunc ? deactivatorFunc : Particle_DeactivatorAge)
    {
    }

    void Particle::Init(const EmitterConfig& cfg, std::mt19937& random)
    {
        age = 0;
        origin = cfg.origin;

        // Base direction (normalized)
        Vector3 direction = Vector3Normalize(cfg.direction);

        // Get a small random angle to find a random velocity direction.
        float randaX =
            RandomFloat(random, cfg.directionAngle.min, cfg.directionAngle.max) * sage::math::DEGREES_TO_RADIANS;
        float randaY =
            RandomFloat(random, cfg.directionAngle.min, cfg.directionAngle.max) * sage::math::DEGREES_TO_RADIANS;
        float randaZ =
            RandomFloat(random, cfg.directionAngle.min, cfg.directionAngle.max) * sage::math::DEGREES_TO_RADIANS;

        // Rotate base direction with the given angles.
        direction = RotateV3(direction, randaX, randaY, randaZ);

        // Get a random value for velocity range (direction is normalized).
        float randv = RandomFloat(random, cfg.velocity.min, cfg.velocity.max);

        // Multiply direction with factor to set actual velocity in the Particle.
        velocity = Vector3Scale(direction, randv);

        // Get a small random angle to rotate the velocity vector.
        randaX =
            RandomFloat(random, cfg.velocityAngle.min, cfg.velocityAngle.max) * sage::math::DEGREES_TO_RADIANS;
        randaY =
            RandomFloat(random, cfg.velocityAngle.min, cfg.velocityAngle.max) * sage::math::DEGREES_TO_RADIANS;
        randaZ =
            RandomFloat(random, cfg.velocityAngle.min, cfg.velocityAngle.max) * sage::math::DEGREES_TO_RADIANS;

        // Rotate velocity vector with given angles.
        velocity = RotateV3(velocity, randaX, randaY, randaZ);

        // Get a smaller random value for origin offset and apply it to position.
        float rando = RandomFloat(random, cfg.offset.min, cfg.offset.max) * 0.1f;
        position.x = cfg.origin.x + direction.x * rando;
        position.y = cfg.origin.y + direction.y * rando;
        position.z = cfg.origin.z + direction.z * rando;

        // Get a random value for the intrinsic particle acceleration
        float rands = RandomFloat(random, cfg.originAcceleration.min, cfg.originAcceleration.max);
        originAcceleration = rands;
        externalAcceleration = cfg.externalAcceleration;
        ttl = RandomFloat(random, cfg.age.min, cfg.age.max);
        active = true;
        size = cfg.size;
    }

    void Particle::Update(float dt, const ParticleCurve& speedOverLifetime)
    {
        if (!active)
        {
            return;
        }

        age += dt;

        if (particle_Deactivator(this))
        {
            active = false;
            return;
        }

        Vector3 toOrigin = Vector3Normalize(Vector3Subtract(origin, position));

        // Update velocity by internal acceleration.
        velocity = Vector3Add(velocity, Vector3Scale(toOrigin, originAcceleration * dt));

        // Update velocity by external acceleration.
        velocity = Vector3Add(velocity, Vector3Scale(externalAcceleration, dt));

        // Apply centripetal force to create a swirl effect
        //	Vector3 centripetalForce = { -toOrigin.z, 0, toOrigin.x };
        //	velocity = Vector3Add(velocity, Vector3Scale(centripetalForce, originAcceleration * dt));

        // Update position by velocity.
        position = Vector3Add(position, Vector3Scale(velocity, dt * speedOverLifetime.Evaluate(age / ttl)));
    }

    // Emitter constructor
    Emitter::Emitter(EmitterConfig cfg)
        : config(std::move(cfg)), random(config.randomSeed ? *config.randomSeed : std::random_device{}())
    {
        offset.x = static_cast<float>(config.texture.width / 2);
        offset.y = static_cast<float>(config.texture.height / 2);
        particles.reserve(config.capacity);
        for (size_t i = 0; i < config.capacity; i++)
        {
            particles.push_back(std::make_unique<Particle>(config.particle_Deactivator));
        }
    }

    // Emitter_Reinit reinits the given Emitter with a new EmitterConfig.
    bool Emitter::Reinit(const EmitterConfig& cfg)
    {
        if (cfg.capacity > config.capacity)
        {
            particles.reserve(cfg.capacity);
            for (size_t i = config.capacity; i < cfg.capacity; i++)
            {
                particles.push_back(std::make_unique<Particle>(cfg.particle_Deactivator));
            }
        }
        else if (cfg.capacity < config.capacity)
        {
            particles.resize(cfg.capacity);
        }

        config = cfg;
        offset = {
            .x = static_cast<float>(config.texture.width) / 2.0f,
            .y = static_cast<float>(config.texture.height) / 2.0f};

        for (size_t i = 0; i < config.capacity; i++)
        {
            particles.at(i)->particle_Deactivator =
                config.particle_Deactivator ? config.particle_Deactivator : Particle_DeactivatorAge;
        }

        return true;
    }

    // Emitter_Start activates Particle emission.
    void Emitter::Start()
    {
        isEmitting = true;
    }

    // Emitter_Stop deactivates Particle emission.
    void Emitter::Stop()
    {
        isEmitting = false;
    }

    // Emitter_Burst emits a specified amount of particles at once,
    // ignoring the state of e->isEmitting. Use this for singular events
    // instead of continuous output.
    void Emitter::Burst()
    {
        size_t emitted = 0;
        const int amount = std::uniform_int_distribution<int>(config.burst.min, config.burst.max)(random);
        if (amount <= 0) return;

        for (size_t i = 0; i < config.capacity; i++)
        {
            auto& p = particles.at(i);
            if (!p->active)
            {
                p->Init(config, random);
                p->position = config.origin;
                emitted++;
            }
            if (std::cmp_greater_equal(emitted, amount))
            {
                return;
            }
        }
    }

    // Emitter_Update updates all particles and returns
    // the current amount of active particles.
    void Emitter::Update(float dt)
    {
        size_t emitNow = 0;

        if (isEmitting)
        {
            mustEmit += dt * static_cast<float>(config.emissionRate);
            mustEmit = std::min(mustEmit, static_cast<float>(config.capacity));
            emitNow = static_cast<size_t>(mustEmit); // floor
        }

        for (size_t i = 0; i < config.capacity; i++)
        {
            auto& p = particles.at(i);
            if (p->active)
            {
                p->Update(dt, config.speedOverLifetime);
            }
            else if (isEmitting && emitNow > 0)
            {
                // emit new particles here
                p->Init(config, random);
                p->Update(dt, config.speedOverLifetime);
                emitNow--;
                mustEmit--;
            }
        }
    }

    Color Emitter::ParticleColor(const Particle& particle) const
    {
        const float age = particle.ttl > 0 ? particle.age / particle.ttl : 1.0f;
        auto color = config.colorOverLifetime.Evaluate(age, config.startColor, config.endColor);
        color.a = static_cast<unsigned char>(
            std::clamp(static_cast<float>(color.a) * config.opacityOverLifetime.Evaluate(age), 0.0f, 255.0f));
        return color;
    }

    float Emitter::ParticleSize(const Particle& particle) const
    {
        return particle.size *
               config.sizeOverLifetime.Evaluate(particle.ttl > 0 ? particle.age / particle.ttl : 1.0f);
    }

    namespace
    {
        std::vector<const Particle*> ActiveParticles(const Emitter& emitter)
        {
            std::vector<const Particle*> active;
            active.reserve(emitter.particles.size());
            for (const auto& particle : emitter.particles)
                if (particle->active) active.push_back(particle.get());
            return active;
        }

        void DrawParticleBillboards(
            const Emitter& emitter, Camera3D& camera, const std::vector<const Particle*>& particles)
        {
            BeginBlendMode(emitter.config.blendMode);
            // Flush opaque geometry before changing depth writes. Transparent texels must
            // still depth-test against the scene, but must not occlude later particles.
            rlDrawRenderBatchActive();
            rlDisableDepthMask();
            for (const auto* particle : particles)
                DrawBillboard(
                    camera,
                    emitter.config.texture,
                    particle->position,
                    emitter.ParticleSize(*particle),
                    emitter.ParticleColor(*particle));
            // Submit the billboards before restoring the depth state for later geometry.
            rlDrawRenderBatchActive();
            rlEnableDepthMask();
            EndBlendMode();
        }
    } // namespace

    void Emitter::DrawNearestFirst(Camera3D* const camera) const
    {
        auto active = ActiveParticles(*this);
        std::ranges::sort(active, [&camera](const Particle* a, const Particle* b) {
            return Vector3Distance(a->position, camera->position) < Vector3Distance(b->position, camera->position);
        });
        DrawParticleBillboards(*this, *camera, active);
    }

    void Emitter::DrawNearestFirst(Camera3D* const camera, const Shader& shader) const
    {
        BeginShaderMode(shader);
        DrawNearestFirst(camera);
        EndShaderMode();
    }

    void Emitter::DrawOldestFirst(Camera3D* const camera) const
    {
        auto active = ActiveParticles(*this);
        std::ranges::sort(active, [](const Particle* a, const Particle* b) { return a->age < b->age; });
        DrawParticleBillboards(*this, *camera, active);
    }

    void Emitter::DrawOldestFirst(Camera3D* const camera, const Shader& shader) const
    {
        BeginShaderMode(shader);
        DrawOldestFirst(camera);
        EndShaderMode();
    }

    // Alpha blending requires back-to-front order in camera space.
    void Emitter::Draw(Camera3D* const camera) const
    {
        auto active = ActiveParticles(*this);
        const auto forward = Vector3Subtract(camera->target, camera->position);
        std::ranges::sort(active, [&forward](const Particle* a, const Particle* b) {
            return Vector3DotProduct(a->position, forward) > Vector3DotProduct(b->position, forward);
        });
        DrawParticleBillboards(*this, *camera, active);
    }

    void Emitter::Draw(Camera3D* const camera, const Shader& shader) const
    {
        BeginShaderMode(shader);
        Draw(camera);
        EndShaderMode();
    }

    // ParticleSystem constructor
    ParticleSystem::ParticleSystem(Camera& _camera) : camera(_camera)
    {
        emitters.reserve(capacity);
    }

    // ParticleSystem_Update runs Emitter_Update on all registered Emitters.
    void ParticleSystem::Update(float dt)
    {
        for (size_t i = 0; i < length; i++)
        {
            emitters.at(i)->Update(dt);
        }
    }

    // ParticleSystem_Register registers an emitter to the system.
    // The emitter will be controlled by all particle system functions.
    // Returns true on success and false otherwise.
    bool ParticleSystem::Register(std::unique_ptr<Emitter> emitter)
    {
        // If there is no space for another emitter we have to realloc.
        if (length >= capacity)
        {
            // Double capacity.
            emitters.reserve(capacity * 2);
            capacity *= 2;
        }

        // Now the new Emitter can be registered.
        emitters.push_back(std::move(emitter));
        length++;

        return true;
    }

    // ParticleSystem_Deregister deregisters an Emitter by its pointer.
    // Returns true on success and false otherwise.
    bool ParticleSystem::Deregister(Emitter* emitter)
    {
        for (size_t i = 0; i < length; i++)
        {
            if (emitters.at(i).get() == emitter)
            {
                // Remove this emitter by replacing its pointer with the
                // last pointer, if it is not the only Emitter.
                std::swap(emitters.at(i), emitters.back());
                emitters.pop_back();
                length--;
                return true;
            }
        }
        // Emitter not found.
        return false;
    }

    // ParticleSystem_SetOrigin sets the origin for all registered Emitters.
    void ParticleSystem::SetOrigin(Vector3 _origin)
    {
        origin = _origin;
        for (auto& emitter : emitters)
        {
            emitter->config.origin = _origin;
        }
    }

    void ParticleSystem::SetDirection(Vector3 _direction)
    {
        for (auto& emitter : emitters)
        {
            emitter->config.direction = _direction;
        }
    }

    // ParticleSystem_Start runs Emitter_Start on all registered Emitters.
    void ParticleSystem::Start()
    {
        for (auto& emitter : emitters)
        {
            emitter->Start();
        }
    }

    // ParticleSystem_Stop runs Emitter_Stop on all registered Emitters.
    void ParticleSystem::Stop()
    {
        for (auto& emitter : emitters)
        {
            emitter->Stop();
        }
    }

    // ParticleSystem_Burst runs Emitter_Burst on all registered Emitters.
    void ParticleSystem::Burst()
    {
        for (auto& emitter : emitters)
        {
            emitter->Burst();
        }
    }

    // ParticleSystem_Draw runs Emitter_Draw on all registered Emitters.
    void ParticleSystem::Draw() const
    {
        std::vector<Emitter*> activeEmitters;
        for (const auto& emitter : emitters)
        {
            if (emitter->isEmitting)
            {
                activeEmitters.push_back(emitter.get());
            }
        }

        std::ranges::sort(activeEmitters, [this](const Emitter* a, const Emitter* b) {
            return Vector3Distance(a->config.origin, camera.get().position) <
                   Vector3Distance(b->config.origin, camera.get().position);
        });

        {
            for (auto& emitter : activeEmitters)
            {
                emitter->Draw(&camera.get());
            }
        }
    }
    void ParticleSystem::Draw(const Shader& shader) const
    {
        std::vector<Emitter*> activeEmitters;
        for (const auto& emitter : emitters)
        {
            if (emitter->isEmitting)
            {
                activeEmitters.push_back(emitter.get());
            }
        }

        std::ranges::sort(activeEmitters, [this](const Emitter* a, const Emitter* b) {
            return Vector3Distance(a->config.origin, camera.get().position) <
                   Vector3Distance(b->config.origin, camera.get().position);
        });

        for (auto& emitter : activeEmitters)
        {
            emitter->Draw(&camera.get(), shader);
        }
    }

    void ParticleSystem::DrawNearestFirst() const
    {

        for (auto& emitter : emitters)
        {
            emitter->DrawNearestFirst(&camera.get());
        }
    }

    void ParticleSystem::DrawNearestFirst(const Shader& shader) const
    {
        std::vector<Emitter*> activeEmitters;
        for (const auto& emitter : emitters)
        {
            if (emitter->isEmitting)
            {
                activeEmitters.push_back(emitter.get());
            }
        }

        std::ranges::sort(activeEmitters, [this](const Emitter* a, const Emitter* b) {
            return Vector3Distance(a->config.origin, camera.get().position) <
                   Vector3Distance(b->config.origin, camera.get().position);
        });

        for (auto& emitter : activeEmitters)
        {
            emitter->DrawNearestFirst(&camera.get(), shader);
        }
    }

    void ParticleSystem::DrawOldestFirst() const
    {
        for (auto& emitter : emitters)
        {
            emitter->DrawOldestFirst(&camera.get());
        }
    }

    void ParticleSystem::DrawOldestFirst(const Shader& shader) const
    {
        std::vector<Emitter*> activeEmitters;
        for (const auto& emitter : emitters)
        {
            if (emitter->isEmitting)
            {
                activeEmitters.push_back(emitter.get());
            }
        }

        std::ranges::sort(activeEmitters, [this](const Emitter* a, const Emitter* b) {
            return Vector3Distance(a->config.origin, camera.get().position) <
                   Vector3Distance(b->config.origin, camera.get().position);
        });

        for (auto& emitter : activeEmitters)
        {
            emitter->DrawOldestFirst(&camera.get(), shader);
        }
    }
} // namespace sage
