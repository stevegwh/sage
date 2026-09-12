#include "engine/components/Collideable.hpp"
#include "engine/components/MoveableActor.hpp"
#include "engine/components/sgTransform.hpp"
#include "engine/systems/ActorMovementSystem.hpp"
#include "engine/systems/NavigationGridSystem.hpp"
#include "engine/systems/TransformSystem.hpp"

#include <array>
#include <iostream>
#include <stdexcept>

namespace
{
    void Require(bool condition, const char* message)
    {
        if (!condition) throw std::runtime_error(message);
    }

    Vector3 Point(float x, float z = 0.5f) { return {x, -1.0f, z}; }

    struct World
    {
        entt::registry registry;
        sage::TransformSystem transforms{&registry};
        sage::NavigationGridSystem grid{&registry, nullptr};
        sage::ActorMovementSystem movement{&registry, &grid};

        explicit World(int size = 12) { grid.Init(size, 1.0f); }

        entt::entity Actor(Vector3 position, float radius = 0.2f)
        {
            const auto entity = registry.create();
            auto& transform = registry.emplace<sage::sgTransform>(entity);
            transforms.SetWorldPos(entity, position);
            registry.emplace<sage::Collideable>(entity,
                BoundingBox{{-radius, 0.0f, -radius}, {radius, 1.0f, radius}}, transform.GetMatrixNoRot());
            auto& actor = registry.emplace<sage::MoveableActor>(entity);
            actor.turnSpeed = 0.0f;
            actor.movementSpeed = 0.25f;
            return entity;
        }

        entt::entity Wall(Vector3 position)
        {
            const auto wall = registry.create();
            grid.MarkSquareAreaOccupied(
                {{position.x - 0.5f, -1.0f, position.z - 0.5f},
                 {position.x + 0.5f, 1.0f, position.z + 0.5f}}, true, wall);
            return wall;
        }

        sage::MoveableActor& ActorData(entt::entity entity) { return registry.get<sage::MoveableActor>(entity); }
        Vector3 Position(entt::entity entity) { return registry.get<sage::sgTransform>(entity).GetWorldPos(); }

        void Route(entt::entity entity, Vector3 destination)
        {
            const auto route = movement.FindRouteToLocation(entity, destination, true);
            Require(!route.empty() && movement.SetRoute(entity, route), "route installation failed");
        }

        void Tick() { movement.Update(0.1f); }
        void Finish(int ticks = 150) { for (int i = 0; i < ticks; ++i) Tick(); }

        bool Overlap(entt::entity a, entt::entity b)
        {
            const auto& lhs = registry.get<sage::Collideable>(a).worldBoundingBox;
            const auto& rhs = registry.get<sage::Collideable>(b).worldBoundingBox;
            return lhs.min.x < rhs.max.x && lhs.max.x > rhs.min.x &&
                   lhs.min.z < rhs.max.z && lhs.max.z > rhs.min.z;
        }

        void RequireUnmarked(entt::entity entity)
        {
            for (const auto& row : grid.GetGridSquares())
                for (const auto& square : row)
                    Require(square.occupant != entity, "travelling actor marked the grid");
        }
    };

    void TestRoutesThroughStandingActors()
    {
        World world;
        // Only a single row is walkable, so routing must pass through the standing actor.
        for (int z = -6; z < 6; ++z)
            if (z != 0)
                for (int x = -6; x < 6; ++x) world.Wall(Point(x + 0.5f, z + 0.5f));
        const auto standing = world.Actor(Point(0.5f));
        const auto moving = world.Actor(Point(-3.5f));
        world.Tick();
        for (bool astar : {false, true})
        {
            const auto route = world.movement.FindRouteToLocation(moving, Point(3.5f), astar, false);
            Require(!route.empty() && Vector3Equals(route.back(), Point(3.5f)), "standing NPC blocked transit");
            Require(world.movement.FindRouteToLocation(moving, Point(0.5f), astar, false).empty(),
                    "strict route accepted an occupied destination");
            const auto fallback = world.movement.FindRouteToLocation(moving, Point(0.5f), astar, true);
            Require(!fallback.empty() && !Vector3Equals(fallback.back(), Point(0.5f)), "no free fallback selected");
        }
        world.Route(moving, Point(3.5f));
        world.RequireUnmarked(moving);
        bool passedThrough = false;
        for (int i = 0; i < 100; ++i)
        {
            world.Tick();
            if (world.ActorData(moving).IsMoving()) world.RequireUnmarked(moving);
            if (world.Overlap(moving, standing))
            {
                passedThrough = true;
                (void)world.movement.FindRouteToLocation(moving, Point(3.5f), true);
                Require(world.grid.CheckSingleSquareOccupant(Point(0.5f)) == standing,
                        "route query overwrote a standing actor");
            }
        }
        Require(passedThrough && world.movement.ReachedDestination(moving), "NPC did not pass through and arrive");
        Require(Vector3Equals(world.Position(moving), Point(3.5f)), "wrong transit destination");
        world.Wall(Point(1.5f));
        Require(world.movement.FindRouteToLocation(moving, Point(-3.5f), true, false).empty(),
                "static wall did not block the route");
    }

    void TestMeetingActors(Vector3 startA, Vector3 endA, Vector3 startB, Vector3 endB)
    {
        World world;
        const auto a = world.Actor(startA);
        const auto b = world.Actor(startB);
        world.Tick();
        world.Route(a, endA);
        world.Route(b, endB);
        bool passedThrough = false;
        for (int i = 0; i < 120; ++i)
        {
            world.Tick();
            passedThrough |= world.Overlap(a, b);
            if (world.ActorData(a).IsMoving()) world.RequireUnmarked(a);
            if (world.ActorData(b).IsMoving()) world.RequireUnmarked(b);
        }
        Require(passedThrough, "actors did not overlap while travelling");
        Require(world.movement.ReachedDestination(a) && world.movement.ReachedDestination(b), "traffic got stuck");
        Require(!world.Overlap(a, b), "actors stopped on top of each other");
    }

    void TestSharedDestination(bool reverseCreation)
    {
        World world;
        const auto a = world.Actor(Point(reverseCreation ? 3.5f : -2.5f));
        const auto b = world.Actor(Point(reverseCreation ? -2.5f : 3.5f));
        world.Tick();
        world.Route(a, Point(0.5f));
        world.Route(b, Point(0.5f));
        int arrivals = 0;
        auto checkArrival = [&](entt::entity entity) {
            ++arrivals;
            Require(world.grid.CheckSingleSquareOccupant(world.Position(entity)) == entity,
                    "arrival published before claiming destination");
            Require(!world.Overlap(a, b) || world.ActorData(a == entity ? b : a).IsMoving(),
                    "arrival overlapped another stopped actor");
        };
        auto subA = world.ActorData(a).onDestinationReached.Subscribe(checkArrival);
        auto subB = world.ActorData(b).onDestinationReached.Subscribe(checkArrival);
        world.Finish();
        Require(arrivals == 2 && !world.Overlap(a, b), "shared destination was not adjusted");
        Require(Vector3Equals(world.Position(a), Point(0.5f)) || Vector3Equals(world.Position(b), Point(0.5f)),
                "neither actor used the original destination");
        subA.UnSubscribe();
        subB.UnSubscribe();
    }

    void TestCancellationDuringOverlap()
    {
        World world;
        const auto standing = world.Actor(Point(0.5f));
        const auto moving = world.Actor(Point(-2.5f));
        world.Tick();
        world.Route(moving, Point(3.5f));
        int arrivals = 0;
        auto sub = world.ActorData(moving).onDestinationReached.Subscribe([&](entt::entity) { ++arrivals; });
        for (int i = 0; i < 30 && !world.Overlap(moving, standing); ++i) world.Tick();
        Require(world.Overlap(moving, standing), "cancellation setup did not overlap");
        world.ActorData(moving).ClearRoute(moving); // Same entry point used by C#.
        world.Finish();
        Require(arrivals == 0, "cancelled route published arrival");
        Require(!world.Overlap(moving, standing) && world.movement.ReachedDestination(moving),
                "cancelled actor did not find a stopping place");
        Require(Vector3Equals(world.Position(standing), Point(0.5f)), "cancelled actor displaced standing actor");
        sub.UnSubscribe();
    }

    void TestNoFreeStoppingPlace()
    {
        World world(2);
        world.Wall(Point(-0.5f, -0.5f));
        world.Wall(Point(0.5f, -0.5f));
        world.Wall(Point(-0.5f, 0.5f));
        const auto standing = world.Actor(Point(0.5f));
        world.Tick();
        const auto moving = world.Actor(Point(0.5f));
        Require(world.movement.SetRoute(moving, std::array{Point(0.5f)}), "pending route failed");
        int arrivals = 0;
        auto sub = world.ActorData(moving).onDestinationReached.Subscribe([&](entt::entity) { ++arrivals; });
        world.Finish(10);
        Require(arrivals == 0 && !world.movement.ReachedDestination(moving), "blocked actor reported arrival");
        Require(world.grid.CheckSingleSquareOccupant(Point(0.5f)) == standing, "pending actor stole occupancy");
        Require(world.movement.FindRouteToLocation(moving, Point(0.5f), true).empty(),
                "occupied start returned as successful fallback");
        world.registry.destroy(standing);
        world.Finish(10);
        Require(arrivals == 1 && world.movement.ReachedDestination(moving), "released destination was not retried");
        sub.UnSubscribe();
    }

    void TestStaticOwnershipAndFootprint()
    {
        World world;
        const auto wall = world.Wall(Point(0.5f));
        const auto actor = world.Actor(Point(0.5f));
        world.Finish(); // An overlapping spawn must move clear without overwriting the wall.
        Require(world.grid.CheckSingleSquareOccupant(Point(0.5f)) == wall, "NPC erased static occupancy");
        Require(world.movement.ReachedDestination(actor), "spawn did not move clear of static obstacle");
        const auto large = world.Actor(Point(3.5f), 0.6f);
        world.Tick();
        const auto route = world.movement.FindRouteToLocation(actor, Point(2.5f), true);
        Require(!route.empty() && !Vector3Equals(route.back(), Point(2.5f)), "destination ignored full NPC footprint");
        Require(world.grid.CheckSingleSquareOccupant(Point(2.5f)) == large, "large footprint not marked");
    }

    void TestRouteStartedDuringArrival()
    {
        World world;
        const auto actor = world.Actor(Point(-2.5f));
        world.Tick();
        world.Route(actor, Point(0.5f));
        int arrivals = 0;
        auto sub = world.ActorData(actor).onDestinationReached.Subscribe([&](entt::entity entity) {
            if (++arrivals == 1) world.Route(entity, Point(3.5f));
        });
        for (int i = 0; i < 100; ++i)
        {
            world.Tick();
            if (world.ActorData(actor).IsMoving()) world.RequireUnmarked(actor);
        }
        Require(arrivals == 2 && Vector3Equals(world.Position(actor), Point(3.5f)), "arrival replaced new route");
        sub.UnSubscribe();
    }

    void TestOffsetSearchWindow()
    {
        World world(400);
        const auto actor = world.Actor(Point(60.5f, 70.5f));
        world.ActorData(actor).pathfindingBounds = 10;
        world.Tick();
        // This wall forces multiple turns so traceback must read window-relative parents.
        for (int z = 66; z <= 74; ++z) world.Wall(Point(63.5f, z + 0.5f));
        const auto destination = Point(66.5f, 70.5f);
        for (bool astar : {false, true})
        {
            const auto route = world.movement.FindRouteToLocation(actor, destination, astar, false);
            Require(route.size() > 1 && Vector3Equals(route.back(), destination),
                    "offset search window did not trace a route around the wall");
            for (auto point : route)
                Require(world.grid.CheckEntityAreaUnoccupied(actor, point), "route corner overlaps the wall");
        }
        world.Wall(destination);
        for (bool astar : {false, true})
        {
            Require(world.movement.FindRouteToLocation(actor, destination, astar, false).empty(),
                    "offset strict search accepted a blocked destination");
            const auto route = world.movement.FindRouteToLocation(actor, destination, astar, true);
            Require(!route.empty() && !Vector3Equals(route.back(), destination) &&
                    world.grid.CheckEntityAreaUnoccupied(actor, route.back()),
                    "offset fallback search did not find a free stopping place");
        }
        Require(world.grid.AStarPathfind(actor, world.Position(actor), destination, {0, 0}, {10, 10}).empty(),
                "search window excluding the start was accepted");
        Require(world.grid.AStarPathfind(actor, world.Position(actor), destination, {300, 300}, {200, 200}).empty(),
                "inverted search window was accepted");
    }
}

int main()
{
    try
    {
        TestRoutesThroughStandingActors();
        TestMeetingActors(Point(-3.5f), Point(3.5f), Point(2.5f), Point(-2.5f));
        TestMeetingActors(Point(-2.5f), Point(3.5f), Point(0.5f, -2.5f), Point(0.5f, 3.5f));
        TestMeetingActors(Point(-2.5f), Point(2.5f), Point(-1.5f), Point(2.5f));
        TestSharedDestination(false);
        TestSharedDestination(true);
        TestCancellationDuringOverlap();
        TestNoFreeStoppingPlace();
        TestStaticOwnershipAndFootprint();
        TestRouteStartedDuringArrival();
        TestOffsetSearchWindow();
        std::cout << "Actor movement tests passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Actor movement test failed: " << error.what() << '\n';
        return 1;
    }
}
