//
// Created by steve on 05/01/2025.
//

#pragma once

#include <cassert>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace sage
{
    using SubscriberId = int;

    class EventBase;

    // Wrapper class to track a subscription to an event. Allows easy unsubscribing.
    class Subscription
    {
        EventBase* event = nullptr;
        SubscriberId id = -1;

      public:
        bool IsActive();
        void UnSubscribe();
        ~Subscription();
        explicit Subscription(EventBase* _event, SubscriberId _id);
        Subscription() = default;
    };

    class EventBase
    {
        // Force unsubscribing via the Subscription class
        virtual void unSubscribe(SubscriberId id) = 0;

      public:
        virtual ~EventBase() = default;

        friend class Subscription;
    };

    template <typename... Args>
    class Event : public EventBase
    {
        using Callback = std::function<void(Args...)>;

        unsigned int count = 0;
        mutable std::unordered_map<unsigned int, Callback> subscriptions;

        // Publish iterates `subscriptions` directly rather than copying it every fire (this
        // event type is published per-entity per-frame in places). To keep that iteration
        // valid when a callback (un)subscribes mid-publish, mutations are deferred while a
        // publish is in flight and applied when the outermost publish returns. This preserves
        // the old copy-snapshot semantics: subscribers present when a publish began are each
        // called once; ones added during it first run on the next publish.
        mutable int publishDepth = 0;
        mutable std::unordered_map<unsigned int, Callback> pendingAdds;
        mutable std::vector<unsigned int> pendingRemovals;

        void unSubscribe(SubscriberId id) override
        {
            if (id < 0) return;
            if (publishDepth > 0)
            {
                pendingAdds.erase(id); // cancel a not-yet-applied add
                pendingRemovals.push_back(id);
                return;
            }
            subscriptions.erase(id);
        }

        void flushPending() const
        {
            for (const auto id : pendingRemovals) subscriptions.erase(id);
            pendingRemovals.clear();
            for (auto& [key, callback] : pendingAdds) subscriptions.insert_or_assign(key, std::move(callback));
            pendingAdds.clear();
        }

      public:
        Subscription Subscribe(Callback func)
        {
            const auto key = ++count;
            // Inserting into `subscriptions` mid-publish could rehash and invalidate the
            // Publish iterator, so defer the insert until the publish finishes.
            if (publishDepth > 0)
                pendingAdds.emplace(key, std::move(func));
            else
                subscriptions.emplace(key, std::move(func));

            return Subscription(this, key);
        }

        void Publish(Args... args) const
        {
            ++publishDepth;
            try
            {
                for (const auto& [key, callback] : subscriptions)
                {
                    callback(args...);
                }
            }
            catch (...)
            {
                if (--publishDepth == 0) flushPending();
                throw;
            }
            if (--publishDepth == 0) flushPending();
        }

        Event() = default;
        Event(const Event&) = delete;
        Event& operator=(const Event&) = delete;
        // Non-movable by design: a Subscription holds a raw EventBase* back-pointer, so
        // moving an Event would dangle every outstanding Subscription. Components that own an
        // Event (Animation, MoveableActor, …) are therefore non-movable too and must be
        // swapped via remove + emplace rather than reassigned.
        Event(Event&&) = delete;
        Event& operator=(Event&&) = delete;

        ~Event() override = default;
    };

} // namespace sage
