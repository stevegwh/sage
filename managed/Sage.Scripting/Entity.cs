namespace Sage;

/// <summary>A lightweight handle to an entity owned by the native ECS.</summary>
public readonly struct Entity(uint id) : IEquatable<Entity>
{
    public static Entity None => new(uint.MaxValue);

    public uint Id { get; } = id;
    public bool Exists => NativeApi.EntityExists(Id);
    public string Name => GetComponent<Transform>() is { } transform ? transform.Name : string.Empty;
    public bool HasRoute => NativeApi.HasRoute(Id);
    public void ClearRoute() => NativeApi.ClearRoute(Id);

    public T? GetComponent<T>() where T : struct, IComponent<T>
    {
        var component = T.Create(this);
        return component.Exists ? component : null;
    }

    public T? GetScript<T>() where T : Script => ScriptRuntime.GetScript<T>(Id);

    public bool TryGetPosition(out Vector3 position)
    {
        position = default;
        return GetComponent<Transform>() is { } transform && transform.TryGetPosition(out position);
    }

    public bool SetPosition(Vector3 position) =>
        GetComponent<Transform>() is { } transform && transform.SetPosition(position);

    public bool TryGetRotation(out Vector3 rotation)
    {
        rotation = default;
        return GetComponent<Transform>() is { } transform && transform.TryGetRotation(out rotation);
    }

    public bool SetRotation(Vector3 rotation) =>
        GetComponent<Transform>() is { } transform && transform.SetRotation(rotation);

    public bool TryGetScale(out Vector3 scale)
    {
        scale = default;
        return GetComponent<Transform>() is { } transform && transform.TryGetScale(out scale);
    }

    public bool SetScale(Vector3 scale) =>
        GetComponent<Transform>() is { } transform && transform.SetScale(scale);

    public bool TryGetForward(out Vector3 forward)
    {
        forward = default;
        return GetComponent<Transform>() is { } transform && transform.TryGetForward(out forward);
    }

    public bool TryPathfindTo(Vector3 destination, bool aStar = false, bool findClosestReachable = true) =>
        NativeApi.TryPathfind(Id, destination, aStar, findClosestReachable);

    public bool PlayAnimation(string clip, int speed = 1) =>
        GetComponent<Animation>() is { } animation && animation.Play(clip, speed);
    public bool PlayOneShot(string clip, int speed = 1) =>
        GetComponent<Animation>() is { } animation && animation.PlayOneShot(clip, speed);

    public bool Equals(Entity other) => Id == other.Id;
    public override bool Equals(object? value) => value is Entity entity && Equals(entity);
    public override int GetHashCode() => (int)Id;
    public override string ToString() => $"Entity({Id})";

    public static bool operator ==(Entity left, Entity right) => left.Equals(right);
    public static bool operator !=(Entity left, Entity right) => !left.Equals(right);
}

/// <summary>A blittable vector shared by native engine operations and gameplay code.</summary>
public readonly struct Vector3(float x, float y, float z)
{
    public float X { get; } = x;
    public float Y { get; } = y;
    public float Z { get; } = z;

    /// <summary>Gets the vector's magnitude.</summary>
    public float Length => NativeApi.Vector3Length(this);
    /// <summary>Gets a unit-length copy, or the zero vector when this vector has zero length.</summary>
    public Vector3 Normalized => NativeApi.Vector3Normalized(this);

    public static float Dot(Vector3 lhs, Vector3 rhs) => NativeApi.Vector3Dot(lhs, rhs);
    /// <summary>Returns the unsigned angle between two vectors in degrees.</summary>
    public static float Angle(Vector3 from, Vector3 to) => NativeApi.Vector3Angle(from, to);

    public static Vector3 operator +(Vector3 left, Vector3 right) =>
        new(left.X + right.X, left.Y + right.Y, left.Z + right.Z);
    public static Vector3 operator -(Vector3 left, Vector3 right) =>
        new(left.X - right.X, left.Y - right.Y, left.Z - right.Z);
    public static Vector3 operator *(Vector3 value, float scale) =>
        new(value.X * scale, value.Y * scale, value.Z * scale);

    public override string ToString() => $"({X:0.###}, {Y:0.###}, {Z:0.###})";
}
