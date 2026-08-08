namespace Sage;

/// <summary>Base class for a managed script attached to an engine entity.</summary>
public abstract class Script
{
    public Entity Entity { get; internal set; }

    protected T? GetComponent<T>() where T : struct, IComponent<T> => Entity.GetComponent<T>();

    protected virtual void Awake() { }
    protected virtual void OnEnable() { }
    protected virtual void Start() { }
    protected virtual void Update(float deltaTime) { }
    protected virtual void OnDisable() { }
    protected virtual void OnDestroy() { }
    protected virtual void OnTriggerEnter(Entity other) { }
    protected virtual void OnTriggerStay(Entity other) { }
    protected virtual void OnTriggerExit(Entity other) { }
    internal void InvokeAwake() => Awake();
    internal void InvokeOnEnable() => OnEnable();
    internal void InvokeStart() => Start();
    internal void InvokeUpdate(float deltaTime) => Update(deltaTime);
    internal void InvokeOnDisable() => OnDisable();
    internal void InvokeOnDestroy() => OnDestroy();

    internal void InvokeTrigger(TriggerEvent eventType, Entity other)
    {
        if (eventType == TriggerEvent.Enter) OnTriggerEnter(other);
        if (eventType == TriggerEvent.Stay) OnTriggerStay(other);
        if (eventType == TriggerEvent.Exit) OnTriggerExit(other);
    }
}
