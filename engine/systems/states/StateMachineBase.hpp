//
// CRTP base for variant-based state machines.
//
// Derived must provide:
//   - private void onEnter(StateAlt&, entt::entity);   // one overload per variant alternative
//   - private void onExit(StateAlt&, entt::entity);    // one overload per variant alternative
//   - friend class StateMachineBase<Derived, StateComponent>;
//
// Derived may hide:
//   - static bool isLocked(const StateComponent&)      // short-circuits transitions while
//                                                       // certain alternatives are current
//
// StateComponent must provide:
//   - Variant member named `current`
//   - void RemoveAllSubscriptions();
//

#pragma once

#include "entt/entt.hpp"

#include <utility>
#include <variant>

namespace sage
{
    template <typename Derived, typename StateComponent>
    class StateMachineBase
    {
      protected:
        entt::registry* registry;

        explicit StateMachineBase(entt::registry* _registry) : registry(_registry)
        {
        }

        // Default lock policy — never locked.
        static bool isLocked(const StateComponent&)
        {
            return false;
        }

      public:
        template <typename NewState>
        void ChangeState(entt::entity entity, NewState newState = {})
        {
            auto& state = registry->get<StateComponent>(entity);
            if (Derived::isLocked(state)) return;
            std::visit(
                [this, entity](auto& cur) { static_cast<Derived*>(this)->onExit(cur, entity); },
                state.current);
            state.RemoveAllSubscriptions();
            state.current = std::move(newState);
            static_cast<Derived*>(this)->onEnter(std::get<NewState>(state.current), entity);
        }
    };
} // namespace sage
