namespace Sage;

/// <summary>A generated handle to a native ECS component.</summary>
public interface IComponent<TSelf> where TSelf : struct, IComponent<TSelf>
{
    static abstract TSelf Create(Entity entity);
    bool Exists { get; }
}
