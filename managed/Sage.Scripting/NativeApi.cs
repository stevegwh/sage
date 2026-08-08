using System.Runtime.InteropServices;
using System.Text;

namespace Sage;

internal enum LogLevel
{
    Info,
    Warning,
    Error
}

public enum ScriptEvent
{
    TriggerEnter,
    TriggerStay,
    TriggerExit,
    MovementStarted,
    DestinationReached,
    DestinationUnreachable,
    MovementCancelled,
    PathChanged,
    AnimationStarted,
    AnimationEnded,
    AnimationUpdated
}

[StructLayout(LayoutKind.Sequential)]
public unsafe struct NativeApiTable
{
    public uint Version;
    public uint Size;
    public nint Context;
    public delegate* unmanaged[Cdecl]<nint, int, byte*, void> Log;
    public delegate* unmanaged[Cdecl]<nint, uint, byte> EntityExists;
    public delegate* unmanaged[Cdecl]<nint, byte*, uint*, byte> FindFirstWithArchetype;
    public delegate* unmanaged[Cdecl]<nint, float, float, float, uint*, byte> GetNavigationSurfaceAt;
    public delegate* unmanaged[Cdecl]<nint, uint, byte> HasRoute;
    public delegate* unmanaged[Cdecl]<nint, uint, float, float, float, byte, byte, byte> TryPathfind;
    public delegate* unmanaged[Cdecl]<nint, byte*, float, float, float, float, float, float, byte, uint*, byte> SpawnFlatpack;
    public delegate* unmanaged[Cdecl]<nint, uint, uint, int, byte> SubscribeEvent;
    public delegate* unmanaged[Cdecl]<nint, uint, ulong, byte> HasComponent;
    public delegate* unmanaged[Cdecl]<nint, uint, ulong, ulong, NativeScriptValue*, byte> GetComponentProperty;
    public delegate* unmanaged[Cdecl]<nint, uint, ulong, ulong, NativeScriptValue*, byte> SetComponentProperty;
    public delegate* unmanaged[Cdecl]<nint, uint, ulong, ulong, NativeScriptValue*, uint, NativeScriptValue*, byte> InvokeComponentMethod;
}

/// <summary>Pointer to the game-owned extension table for a Play session.</summary>
public static class NativeExtension
{
    public static nint Address { get; internal set; }
}

internal static unsafe class NativeApi
{
    internal const uint CurrentVersion = 3;

    private static NativeApiTable api;

    internal static void Set(NativeApiTable value) => api = value;
    internal static void Clear() => api = default;

    internal static void Log(LogLevel level, string message)
    {
        if (api.Log == null)
        {
            Console.WriteLine(message);
            return;
        }

        var bytes = Encoding.UTF8.GetBytes(message + '\0');
        fixed (byte* text = bytes) api.Log(api.Context, (int)level, text);
    }

    internal static bool EntityExists(uint entity) =>
        api.EntityExists != null && api.EntityExists(api.Context, entity) != 0;

    internal static Entity FindFirstWithArchetype(string name)
    {
        if (api.FindFirstWithArchetype == null) return Entity.None;
        var text = Encoding.UTF8.GetBytes(name + '\0');
        uint entity;
        fixed (byte* value = text)
        {
            return api.FindFirstWithArchetype(api.Context, value, &entity) != 0 ? new Entity(entity) : Entity.None;
        }
    }

    internal static Entity GetNavigationSurfaceAt(Vector3 position)
    {
        if (api.GetNavigationSurfaceAt == null) return Entity.None;
        uint entity;
        return api.GetNavigationSurfaceAt(api.Context, position.X, position.Y, position.Z, &entity) != 0
            ? new Entity(entity)
            : Entity.None;
    }

    internal static bool HasRoute(uint entity) =>
        api.HasRoute != null && api.HasRoute(api.Context, entity) != 0;

    internal static bool TryPathfind(uint entity, Vector3 destination, bool aStar, bool findClosestReachable) =>
        api.TryPathfind != null &&
        api.TryPathfind(api.Context, entity, destination.X, destination.Y, destination.Z,
            aStar ? (byte)1 : (byte)0, findClosestReachable ? (byte)1 : (byte)0) != 0;

    internal static Entity SpawnFlatpack(string name, Vector3 position, Vector3 rotation, bool hasRotation)
    {
        if (api.SpawnFlatpack == null) return Entity.None;
        var text = Encoding.UTF8.GetBytes(name + '\0');
        uint entity;
        fixed (byte* value = text)
        {
            return api.SpawnFlatpack(api.Context, value, position.X, position.Y, position.Z,
                       rotation.X, rotation.Y, rotation.Z, hasRotation ? (byte)1 : (byte)0, &entity) != 0
                ? new Entity(entity)
                : Entity.None;
        }
    }

    internal static bool SubscribeEvent(uint listener, Entity source, ScriptEvent eventType) =>
        api.SubscribeEvent != null && api.SubscribeEvent(api.Context, listener, source.Id, (int)eventType) != 0;

    internal static bool HasComponent(uint entity, ulong componentId) =>
        api.HasComponent != null && api.HasComponent(api.Context, entity, componentId) != 0;

    internal static bool GetComponentProperty(
        uint entity, ulong componentId, ulong propertyId, NativeScriptValue* destination) =>
        api.GetComponentProperty != null &&
        api.GetComponentProperty(api.Context, entity, componentId, propertyId, destination) != 0;

    internal static bool SetComponentProperty(
        uint entity, ulong componentId, ulong propertyId, NativeScriptValue* value) =>
        api.SetComponentProperty != null &&
        api.SetComponentProperty(api.Context, entity, componentId, propertyId, value) != 0;

    internal static bool InvokeComponentMethod(
        uint entity,
        ulong componentId,
        ulong methodId,
        NativeScriptValue* arguments,
        uint argumentCount,
        NativeScriptValue* result) =>
        api.InvokeComponentMethod != null &&
        api.InvokeComponentMethod(api.Context, entity, componentId, methodId, arguments, argumentCount, result) != 0;
}

public static class World
{
    public static Entity FindFirstWithArchetype(string name) => NativeApi.FindFirstWithArchetype(name);
    public static Entity GetNavigationSurfaceAt(Vector3 position) => NativeApi.GetNavigationSurfaceAt(position);
    public static Entity SpawnFlatpack(string name, Vector3 position) =>
        NativeApi.SpawnFlatpack(name, position, default, false);
    public static Entity SpawnFlatpack(string name, Vector3 position, Vector3 rotation) =>
        NativeApi.SpawnFlatpack(name, position, rotation, true);
}
