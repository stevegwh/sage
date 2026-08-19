using System.Runtime.InteropServices;

namespace Sage;

public enum ScriptValueType : uint
{
    None,
    Boolean,
    Int32,
    UInt32,
    Float,
    String,
    Vector3,
    Entity,
    EntityArray
}

[StructLayout(LayoutKind.Sequential)]
public unsafe struct NativeScriptValue
{
    public ScriptValueType Type;
    public uint Reserved;
    public ulong Integer;
    public float X;
    public float Y;
    public float Z;
    public byte* Text;
    public uint TextCapacity;
    public uint* Entities;
    public uint EntityCapacity;
    public uint EntityCount;
}

/// <summary>One typed argument passed to a C++ component API method.</summary>
public readonly struct ScriptArgument
{
    internal ScriptValueType Type { get; }
    internal ulong Integer { get; }
    internal Vector3 Vector { get; }
    internal string? String { get; }

    private ScriptArgument(ScriptValueType type, ulong integer = 0, Vector3 vector = default, string? text = null)
    {
        Type = type;
        Integer = integer;
        Vector = vector;
        String = text;
    }

    public static ScriptArgument From(bool value) =>
        new(ScriptValueType.Boolean, value ? 1UL : 0UL);
    public static ScriptArgument From(int value) =>
        new(ScriptValueType.Int32, unchecked((ulong)(long)value));
    public static ScriptArgument From(uint value) =>
        new(ScriptValueType.UInt32, value);
    public static ScriptArgument From(float value) =>
        new(ScriptValueType.Float, vector: new Vector3(value, 0.0f, 0.0f));
    public static ScriptArgument From(string value) =>
        new(ScriptValueType.String, text: value);
    public static ScriptArgument From(Vector3 value) =>
        new(ScriptValueType.Vector3, vector: value);
    public static ScriptArgument From(Entity value) =>
        new(ScriptValueType.Entity, value.Id);
}

/// <summary>The typed result of a C++ component API method.</summary>
public readonly struct ScriptResult
{
    public bool Boolean { get; }
    public int Int32 { get; }
    public uint UInt32 { get; }
    public float Float { get; }
    public string? String { get; }
    public Vector3 Vector3 { get; }
    public Entity Entity { get; }
    public Entity[] Entities { get; }

    internal unsafe ScriptResult(NativeScriptValue value)
    {
        Boolean = value.Integer != 0;
        Int32 = unchecked((int)(long)value.Integer);
        UInt32 = (uint)value.Integer;
        Float = value.X;
        String = value.Type == ScriptValueType.String && value.Text != null
            ? Marshal.PtrToStringUTF8((nint)value.Text)
            : null;
        Vector3 = new Vector3(value.X, value.Y, value.Z);
        Entity = new Entity((uint)value.Integer);
        Entities = value.EntityCount == 0 ? [] : new Entity[value.EntityCount];
        for (var index = 0; index < Entities.Length; ++index)
            Entities[index] = new Entity(value.Entities[index]);
    }
}

/// <summary>Low-level access used by generated component wrappers.</summary>
public static unsafe class NativeComponentApi
{
    private const uint TextBufferSize = 4096;
    private const uint EntityBufferSize = 64;

    public static bool HasComponent(uint entity, ulong componentId) =>
        NativeApi.HasComponent(entity, componentId);

    public static Event CreateEvent(uint entity, ulong componentId, ulong eventId) =>
        new(entity, componentId, eventId);

    public static Event<T> CreateEvent<T>(
        uint entity, ulong componentId, ulong eventId, Func<ScriptResult, T> read) =>
        new(entity, componentId, eventId, read);

    public static Event<T1, T2> CreateEvent<T1, T2>(
        uint entity,
        ulong componentId,
        ulong eventId,
        Func<ScriptResult, T1> readFirst,
        Func<ScriptResult, T2> readSecond) =>
        new(entity, componentId, eventId, readFirst, readSecond);

    public static bool TryGetBoolean(uint entity, ulong componentId, ulong propertyId, out bool value)
    {
        var native = new NativeScriptValue { Type = ScriptValueType.Boolean };
        var found = NativeApi.GetComponentProperty(entity, componentId, propertyId, &native);
        value = native.Integer != 0;
        return found;
    }

    public static bool TryGetInt32(uint entity, ulong componentId, ulong propertyId, out int value)
    {
        var native = new NativeScriptValue { Type = ScriptValueType.Int32 };
        var found = NativeApi.GetComponentProperty(entity, componentId, propertyId, &native);
        value = unchecked((int)(long)native.Integer);
        return found;
    }

    public static bool TryGetUInt32(uint entity, ulong componentId, ulong propertyId, out uint value)
    {
        var native = new NativeScriptValue { Type = ScriptValueType.UInt32 };
        var found = NativeApi.GetComponentProperty(entity, componentId, propertyId, &native);
        value = (uint)native.Integer;
        return found;
    }

    public static bool TryGetFloat(uint entity, ulong componentId, ulong propertyId, out float value)
    {
        var native = new NativeScriptValue { Type = ScriptValueType.Float };
        var found = NativeApi.GetComponentProperty(entity, componentId, propertyId, &native);
        value = native.X;
        return found;
    }

    public static bool TryGetString(uint entity, ulong componentId, ulong propertyId, out string value)
    {
        var buffer = stackalloc byte[(int)TextBufferSize];
        var native = new NativeScriptValue
        {
            Type = ScriptValueType.String,
            Text = buffer,
            TextCapacity = TextBufferSize
        };
        var found = NativeApi.GetComponentProperty(entity, componentId, propertyId, &native);
        value = found ? Marshal.PtrToStringUTF8((nint)buffer) ?? string.Empty : string.Empty;
        return found;
    }

    public static bool TryGetVector3(uint entity, ulong componentId, ulong propertyId, out Vector3 value)
    {
        var native = new NativeScriptValue { Type = ScriptValueType.Vector3 };
        var found = NativeApi.GetComponentProperty(entity, componentId, propertyId, &native);
        value = new Vector3(native.X, native.Y, native.Z);
        return found;
    }

    public static bool TryGetEntity(uint entity, ulong componentId, ulong propertyId, out Entity value)
    {
        var native = new NativeScriptValue { Type = ScriptValueType.Entity };
        var found = NativeApi.GetComponentProperty(entity, componentId, propertyId, &native);
        value = found ? new Entity((uint)native.Integer) : Entity.None;
        return found;
    }

    public static bool SetBoolean(uint entity, ulong componentId, ulong propertyId, bool value) =>
        Set(entity, componentId, propertyId,
            new NativeScriptValue { Type = ScriptValueType.Boolean, Integer = value ? 1UL : 0UL });

    public static bool SetInt32(uint entity, ulong componentId, ulong propertyId, int value) =>
        Set(entity, componentId, propertyId,
            new NativeScriptValue { Type = ScriptValueType.Int32, Integer = unchecked((ulong)(long)value) });

    public static bool SetUInt32(uint entity, ulong componentId, ulong propertyId, uint value) =>
        Set(entity, componentId, propertyId,
            new NativeScriptValue { Type = ScriptValueType.UInt32, Integer = value });

    public static bool SetFloat(uint entity, ulong componentId, ulong propertyId, float value) =>
        Set(entity, componentId, propertyId,
            new NativeScriptValue { Type = ScriptValueType.Float, X = value });

    public static bool SetString(uint entity, ulong componentId, ulong propertyId, string value)
    {
        var text = Marshal.StringToCoTaskMemUTF8(value);
        try
        {
            return Set(entity, componentId, propertyId,
                new NativeScriptValue { Type = ScriptValueType.String, Text = (byte*)text });
        }
        finally
        {
            Marshal.FreeCoTaskMem(text);
        }
    }

    public static bool SetVector3(uint entity, ulong componentId, ulong propertyId, Vector3 value) =>
        Set(entity, componentId, propertyId,
            new NativeScriptValue { Type = ScriptValueType.Vector3, X = value.X, Y = value.Y, Z = value.Z });

    public static bool SetEntity(uint entity, ulong componentId, ulong propertyId, Entity value) =>
        Set(entity, componentId, propertyId,
            new NativeScriptValue { Type = ScriptValueType.Entity, Integer = value.Id });

    public static bool TryInvoke(
        uint entity,
        ulong componentId,
        ulong methodId,
        ReadOnlySpan<ScriptArgument> arguments,
        out ScriptResult result)
    {
        var nativeArguments = new NativeScriptValue[arguments.Length];
        var allocatedText = new nint[arguments.Length];
        var resultText = stackalloc byte[(int)TextBufferSize];
        var resultEntities = stackalloc uint[(int)EntityBufferSize];
        try
        {
            for (var index = 0; index < arguments.Length; ++index)
                nativeArguments[index] = ToNative(arguments[index], out allocatedText[index]);
            var nativeResult = new NativeScriptValue
            {
                Text = resultText,
                TextCapacity = TextBufferSize,
                Entities = resultEntities,
                EntityCapacity = EntityBufferSize
            };
            fixed (NativeScriptValue* values = nativeArguments)
            {
                if (!NativeApi.InvokeComponentMethod(
                        entity, componentId, methodId, values, (uint)nativeArguments.Length, &nativeResult))
                {
                    if (nativeResult.Type == ScriptValueType.EntityArray &&
                        nativeResult.EntityCount > nativeResult.EntityCapacity &&
                        nativeResult.EntityCount <= int.MaxValue)
                    {
                        var expandedEntities = new uint[(int)nativeResult.EntityCount];
                        fixed (uint* entities = expandedEntities)
                        {
                            nativeResult.Entities = entities;
                            nativeResult.EntityCapacity = (uint)expandedEntities.Length;
                            if (NativeApi.InvokeComponentMethod(
                                    entity,
                                    componentId,
                                    methodId,
                                    values,
                                    (uint)nativeArguments.Length,
                                    &nativeResult))
                            {
                                result = new ScriptResult(nativeResult);
                                return true;
                            }
                        }
                    }
                    result = default;
                    return false;
                }
            }
            result = new ScriptResult(nativeResult);
            return true;
        }
        finally
        {
            foreach (var text in allocatedText)
                if (text != 0) Marshal.FreeCoTaskMem(text);
        }
    }

    private static bool Set(
        uint entity, ulong componentId, ulong propertyId, NativeScriptValue value) =>
        NativeApi.SetComponentProperty(entity, componentId, propertyId, &value);

    private static NativeScriptValue ToNative(ScriptArgument argument, out nint allocatedText)
    {
        allocatedText = 0;
        var result = new NativeScriptValue
        {
            Type = argument.Type,
            Integer = argument.Integer,
            X = argument.Vector.X,
            Y = argument.Vector.Y,
            Z = argument.Vector.Z
        };
        if (argument.Type != ScriptValueType.String) return result;
        allocatedText = Marshal.StringToCoTaskMemUTF8(argument.String ?? string.Empty);
        result.Text = (byte*)allocatedText;
        return result;
    }
}
