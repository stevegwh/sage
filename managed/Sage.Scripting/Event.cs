namespace Sage;

internal delegate void NativeEventCallback(ReadOnlySpan<NativeScriptValue> values);

/// <summary>A handle that can remove one callback from its native C++ event.</summary>
public sealed class Subscription
{
    private Action? unsubscribe;

    internal Subscription(Action? unsubscribe = null) => this.unsubscribe = unsubscribe;

    public bool IsActive() => unsubscribe != null;

    public void UnSubscribe()
    {
        var callback = unsubscribe;
        unsubscribe = null;
        callback?.Invoke();
    }
}

/// <summary>A C++ component event with no payload.</summary>
public sealed class Event
{
    private readonly uint source;
    private readonly ulong componentId;
    private readonly ulong eventId;

    internal Event(uint source, ulong componentId, ulong eventId)
    {
        this.source = source;
        this.componentId = componentId;
        this.eventId = eventId;
    }

    public Subscription Subscribe(Action callback) => ManagedEventRuntime.Subscribe(
        source, componentId, eventId, values =>
        {
            if (values.IsEmpty) callback();
        });
}

/// <summary>A C++ component event with one payload value.</summary>
public sealed class Event<T>
{
    private readonly uint source;
    private readonly ulong componentId;
    private readonly ulong eventId;
    private readonly Func<ScriptResult, T> read;

    internal Event(uint source, ulong componentId, ulong eventId, Func<ScriptResult, T> read)
    {
        this.source = source;
        this.componentId = componentId;
        this.eventId = eventId;
        this.read = read;
    }

    public Subscription Subscribe(Action<T> callback) => ManagedEventRuntime.Subscribe(
        source, componentId, eventId, values =>
        {
            if (values.Length == 1) callback(read(new ScriptResult(values[0])));
        });
}

/// <summary>A C++ component event with two payload values.</summary>
public sealed class Event<T1, T2>
{
    private readonly uint source;
    private readonly ulong componentId;
    private readonly ulong eventId;
    private readonly Func<ScriptResult, T1> readFirst;
    private readonly Func<ScriptResult, T2> readSecond;

    internal Event(
        uint source,
        ulong componentId,
        ulong eventId,
        Func<ScriptResult, T1> readFirst,
        Func<ScriptResult, T2> readSecond)
    {
        this.source = source;
        this.componentId = componentId;
        this.eventId = eventId;
        this.readFirst = readFirst;
        this.readSecond = readSecond;
    }

    public Subscription Subscribe(Action<T1, T2> callback) => ManagedEventRuntime.Subscribe(
        source, componentId, eventId, values =>
        {
            if (values.Length == 2)
                callback(readFirst(new ScriptResult(values[0])), readSecond(new ScriptResult(values[1])));
        });
}

internal static class ManagedEventRuntime
{
    private sealed record Registration(uint Owner, NativeEventCallback Callback);

    private static readonly Dictionary<ulong, Registration> Registrations = [];
    private static ulong nextId;

    [ThreadStatic]
    private static uint? currentOwner;

    internal static void InvokeFor(uint owner, Action callback)
    {
        var previousOwner = currentOwner;
        currentOwner = owner;
        try { callback(); }
        finally { currentOwner = previousOwner; }
    }

    internal static Subscription Subscribe(
        uint source, ulong componentId, ulong eventId, NativeEventCallback callback)
    {
        if (currentOwner is not uint owner || callback == null) return new Subscription();
        var subscriptionId = ++nextId;
        Registrations.Add(subscriptionId, new Registration(owner, callback));
        if (NativeApi.SubscribeComponentEvent(owner, source, componentId, eventId, subscriptionId))
        {
            return new Subscription(() =>
            {
                Registrations.Remove(subscriptionId);
                NativeApi.UnSubscribeEvent(owner, subscriptionId);
            });
        }
        Registrations.Remove(subscriptionId);
        return new Subscription();
    }

    internal static bool IsOwnedBy(ulong subscriptionId, uint owner) =>
        Registrations.TryGetValue(subscriptionId, out var registration) && registration.Owner == owner;

    internal static unsafe bool Publish(ulong subscriptionId, NativeScriptValue* values, uint count)
    {
        if (!Registrations.TryGetValue(subscriptionId, out var registration) || count > int.MaxValue)
            return false;
        registration.Callback(new ReadOnlySpan<NativeScriptValue>(values, (int)count));
        return true;
    }

    internal static void RemoveOwner(uint owner)
    {
        foreach (var subscriptionId in Registrations
                     .Where(pair => pair.Value.Owner == owner)
                     .Select(pair => pair.Key)
                     .ToArray())
        {
            Registrations.Remove(subscriptionId);
            NativeApi.UnSubscribeEvent(owner, subscriptionId);
        }
    }

    internal static void Clear()
    {
        Registrations.Clear();
        currentOwner = null;
        nextId = 0;
    }
}
