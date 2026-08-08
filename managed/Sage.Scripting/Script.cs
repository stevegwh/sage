namespace Sage;

/// <summary>Base class for a managed script attached to an engine entity.</summary>
public abstract class Script
{
    public Entity Entity { get; internal set; }

    protected bool ListenTo(Entity source, ScriptEvent eventType) =>
        NativeApi.SubscribeEvent(Entity.Id, source, eventType);

    protected virtual void Awake() { }
    protected virtual void OnEnable() { }
    protected virtual void Start() { }
    protected virtual void Update(float deltaTime) { }
    protected virtual void OnDisable() { }
    protected virtual void OnDestroy() { }
    protected virtual void OnTriggerEnter(Entity other) { }
    protected virtual void OnTriggerStay(Entity other) { }
    protected virtual void OnTriggerExit(Entity other) { }
    protected virtual void OnMovementStarted() { }
    protected virtual void OnDestinationReached() { }
    protected virtual void OnDestinationUnreachable(Vector3 destination) { }
    protected virtual void OnMovementCancelled() { }
    protected virtual void OnPathChanged() { }
    protected virtual void OnAnimationStarted() { }
    protected virtual void OnAnimationEnded() { }
    protected virtual void OnAnimationUpdated() { }

    internal void InvokeAwake() => Awake();
    internal void InvokeOnEnable() => OnEnable();
    internal void InvokeStart() => Start();
    internal void InvokeUpdate(float deltaTime) => Update(deltaTime);
    internal void InvokeOnDisable() => OnDisable();
    internal void InvokeOnDestroy() => OnDestroy();

    internal void InvokeEvent(ScriptEvent eventType, Entity other, Vector3 value)
    {
        switch (eventType)
        {
            case ScriptEvent.TriggerEnter: OnTriggerEnter(other); break;
            case ScriptEvent.TriggerStay: OnTriggerStay(other); break;
            case ScriptEvent.TriggerExit: OnTriggerExit(other); break;
            case ScriptEvent.MovementStarted: OnMovementStarted(); break;
            case ScriptEvent.DestinationReached: OnDestinationReached(); break;
            case ScriptEvent.DestinationUnreachable: OnDestinationUnreachable(value); break;
            case ScriptEvent.MovementCancelled: OnMovementCancelled(); break;
            case ScriptEvent.PathChanged: OnPathChanged(); break;
            case ScriptEvent.AnimationStarted: OnAnimationStarted(); break;
            case ScriptEvent.AnimationEnded: OnAnimationEnded(); break;
            case ScriptEvent.AnimationUpdated: OnAnimationUpdated(); break;
        }
    }
}
