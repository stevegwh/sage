using System.Runtime.InteropServices;
using System.Text;

namespace Sage;

internal enum LogLevel
{
    Info,
    Warning,
    Error
}

internal enum TriggerEvent
{
    Enter,
    Stay,
    Exit
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
    public delegate* unmanaged[Cdecl]<nint, uint, void> ClearRoute;
    public delegate* unmanaged[Cdecl]<nint, uint, float, float, float, byte, byte, byte> TryPathfind;
    public delegate* unmanaged[Cdecl]<nint, byte*, float, float, float, float, float, float, byte, uint*, byte> SpawnFlatpack;
    public delegate* unmanaged[Cdecl]<float, float, float, float, float, float, float> Vector3Dot;
    public delegate* unmanaged[Cdecl]<float, float, float, float> Vector3Length;
    public delegate* unmanaged[Cdecl]<float, float, float, float*, float*, float*, void> Vector3Normalize;
    public delegate* unmanaged[Cdecl]<float, float, float, float, float, float, float> Vector3Angle;
    public delegate* unmanaged[Cdecl]<nint, uint, uint, ulong, ulong, ulong, byte> SubscribeComponentEvent;
    public delegate* unmanaged[Cdecl]<nint, uint, ulong, void> UnSubscribeEvent;
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
    internal const uint CurrentVersion = 7;

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

    internal static void ClearRoute(uint entity)
    {
        if (api.ClearRoute != null) api.ClearRoute(api.Context, entity);
    }

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

    internal static float Vector3Dot(Vector3 lhs, Vector3 rhs) =>
        api.Vector3Dot != null
            ? api.Vector3Dot(lhs.X, lhs.Y, lhs.Z, rhs.X, rhs.Y, rhs.Z)
            : 0.0f;

    internal static float Vector3Length(Vector3 value) =>
        api.Vector3Length != null ? api.Vector3Length(value.X, value.Y, value.Z) : 0.0f;

    internal static Vector3 Vector3Normalized(Vector3 value)
    {
        if (api.Vector3Normalize == null) return default;
        float x;
        float y;
        float z;
        api.Vector3Normalize(value.X, value.Y, value.Z, &x, &y, &z);
        return new Vector3(x, y, z);
    }

    internal static float Vector3Angle(Vector3 from, Vector3 to) =>
        api.Vector3Angle != null
            ? api.Vector3Angle(from.X, from.Y, from.Z, to.X, to.Y, to.Z)
            : 0.0f;

    internal static bool SubscribeComponentEvent(
        uint listener, uint source, ulong componentId, ulong eventId, ulong subscriptionId) =>
        api.SubscribeComponentEvent != null &&
        api.SubscribeComponentEvent(api.Context, listener, source, componentId, eventId, subscriptionId) != 0;

    internal static void UnSubscribeEvent(uint listener, ulong subscriptionId)
    {
        if (api.UnSubscribeEvent != null) api.UnSubscribeEvent(api.Context, listener, subscriptionId);
    }

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
