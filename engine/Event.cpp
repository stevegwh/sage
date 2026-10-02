//
// Created by steve on 05/01/2025.
//

#include "Event.hpp"

#include <iostream>

namespace sage
{
    bool Subscription::IsActive()
    {
        return id > -1 && event;
    }

    void Subscription::UnSubscribe()
    {
        if (!IsActive()) return;
        event->get().unSubscribe(id);
        id = -1;
        event = std::nullopt;
    }

    Subscription::~Subscription() = default;

    Subscription::Subscription(EventBase& _event, SubscriberId _id) : event(std::ref(_event)), id(_id)
    {
    }
} // namespace sage