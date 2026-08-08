using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Runtime.Loader;

namespace Sage;

[StructLayout(LayoutKind.Sequential)]
public unsafe struct StartSessionArgs
{
    public byte* GameplayAssemblyPath;
    public NativeApiTable NativeApi;
    public nint GameApi;
}

/// <summary>Stable CoreCLR entry points. Gameplay assemblies are collectible per Play session.</summary>
public static unsafe class ScriptRuntime
{
    private sealed class GameplayLoadContext(string assemblyPath)
        : AssemblyLoadContext($"Sage.Gameplay.{Guid.NewGuid():N}", isCollectible: true)
    {
        private readonly string assemblyDirectory = Path.GetDirectoryName(assemblyPath)!;

        protected override Assembly? Load(AssemblyName assemblyName)
        {
            var scriptingAssembly = typeof(Script).Assembly;
            if (assemblyName.Name == scriptingAssembly.GetName().Name) return scriptingAssembly;
            var dependencyPath = Path.Combine(assemblyDirectory, $"{assemblyName.Name}.dll");
            return File.Exists(dependencyPath) ? LoadFromAssemblyPath(dependencyPath) : null;
        }

        internal Assembly LoadGameplayAssembly(string path)
        {
            using var assembly = File.OpenRead(path);
            var pdbPath = Path.ChangeExtension(path, ".pdb");
            if (!File.Exists(pdbPath)) return LoadFromStream(assembly);
            using var symbols = File.OpenRead(pdbPath);
            return LoadFromStream(assembly, symbols);
        }
    }

    private sealed class ScriptInstance(Script script)
    {
        internal Script Script { get; } = script;
        internal bool Enabled { get; set; }
        internal bool Started { get; set; }
        internal bool Failed { get; set; }
    }

    private static readonly Dictionary<uint, ScriptInstance> Instances = [];
    private static GameplayLoadContext? loadContext;
    private static Assembly? gameplayAssembly;

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    public static int StartSession(StartSessionArgs* args)
    {
        if (args == null || args->GameplayAssemblyPath == null) return -1;
        if (loadContext != null) return -2;
        if (args->NativeApi.Version != NativeApi.CurrentVersion || args->NativeApi.Size != sizeof(NativeApiTable))
        {
            Console.Error.WriteLine("The native and managed C# scripting APIs do not match.");
            return -3;
        }

        NativeApi.Set(args->NativeApi);
        NativeExtension.Address = args->GameApi;
        try
        {
            var path = Marshal.PtrToStringUTF8((nint)args->GameplayAssemblyPath);
            if (string.IsNullOrWhiteSpace(path) || !File.Exists(path))
            {
                Log.Error($"Gameplay assembly does not exist: {path}");
                ClearNativeApis();
                return -4;
            }
            var context = new GameplayLoadContext(path);
            gameplayAssembly = context.LoadGameplayAssembly(path);
            loadContext = context;
            Log.Info($"Loaded gameplay assembly: {Path.GetFileName(path)}");
            return 0;
        }
        catch (Exception error)
        {
            Log.Error($"Could not start the C# gameplay session: {error}");
            loadContext = null;
            gameplayAssembly = null;
            ClearNativeApis();
            return -5;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    public static int CreateScript(uint entity, byte* typeNameUtf8)
    {
        if (gameplayAssembly == null || typeNameUtf8 == null) return -1;
        try
        {
            var typeName = Marshal.PtrToStringUTF8((nint)typeNameUtf8);
            if (string.IsNullOrWhiteSpace(typeName)) return -2;
            var type = gameplayAssembly.GetType(typeName, throwOnError: false, ignoreCase: false);
            if (type == null)
            {
                Log.Error($"C# script type was not found: {typeName}");
                return -3;
            }
            if (type.IsAbstract || !typeof(Script).IsAssignableFrom(type))
            {
                Log.Error($"C# script must be a non-abstract Sage.Script: {typeName}");
                return -4;
            }
            if (Activator.CreateInstance(type) is not Script script)
            {
                Log.Error($"C# script needs a parameterless constructor: {typeName}");
                return -5;
            }
            Destroy(entity);
            script.Entity = new Entity(entity);
            var instance = new ScriptInstance(script);
            Instances.Add(entity, instance);
            Invoke(instance, script.InvokeAwake, "Awake");
            return instance.Failed ? -6 : 0;
        }
        catch (Exception error)
        {
            Log.Error($"Could not create C# script for entity {entity}: {error}");
            return -7;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    public static int UpdateScript(uint entity, float deltaTime, byte enabled)
    {
        if (!Instances.TryGetValue(entity, out var instance) || instance.Failed) return -1;
        var shouldBeEnabled = enabled != 0;
        if (instance.Enabled != shouldBeEnabled)
        {
            instance.Enabled = shouldBeEnabled;
            Invoke(instance, shouldBeEnabled ? instance.Script.InvokeOnEnable : instance.Script.InvokeOnDisable,
                shouldBeEnabled ? "OnEnable" : "OnDisable");
        }
        if (!shouldBeEnabled || instance.Failed) return instance.Failed ? -2 : 0;
        if (!instance.Started)
        {
            instance.Started = true;
            Invoke(instance, instance.Script.InvokeStart, "Start");
        }
        if (!instance.Failed) Invoke(instance, () => instance.Script.InvokeUpdate(deltaTime), "Update");
        return instance.Failed ? -3 : 0;
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    public static int DispatchTrigger(uint listener, int eventType, uint other)
    {
        if (!Instances.TryGetValue(listener, out var instance) || instance.Failed) return -1;
        if (!instance.Enabled) return 0;
        if (eventType < 0 || eventType > (int)TriggerEvent.Exit) return -2;
        Invoke(instance, () => instance.Script.InvokeTrigger((TriggerEvent)eventType, new Entity(other)),
            $"Trigger{(TriggerEvent)eventType}");
        return instance.Failed ? -3 : 0;
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    public static int DispatchEvent(
        uint listener, ulong subscriptionId, NativeScriptValue* values, uint valueCount)
    {
        if (!Instances.TryGetValue(listener, out var instance) || instance.Failed) return -1;
        if (!instance.Enabled) return 0;
        if (!ManagedEventRuntime.IsOwnedBy(subscriptionId, listener)) return -2;
        Invoke(instance, () => ManagedEventRuntime.Publish(subscriptionId, values, valueCount), "Event");
        return instance.Failed ? -3 : 0;
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    public static int DestroyScript(uint entity) { Destroy(entity); return 0; }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    public static int StopSession()
    {
        foreach (var entity in Instances.Keys.ToArray()) Destroy(entity);
        Instances.Clear();
        var context = loadContext;
        gameplayAssembly = null;
        loadContext = null;
        if (context != null) context.Unload();
        ManagedEventRuntime.Clear();
        ClearNativeApis();
        return 0;
    }

    private static void Invoke(ScriptInstance instance, Action callback, string method)
    {
        if (instance.Failed) return;
        try { ManagedEventRuntime.InvokeFor(instance.Script.Entity.Id, callback); }
        catch (Exception error)
        {
            Log.Error($"C# {instance.Script.GetType().FullName}.{method} failed: {error}");
            instance.Failed = true;
        }
    }

    private static void Destroy(uint entity)
    {
        if (!Instances.Remove(entity, out var instance)) return;
        if (instance.Enabled) TryCleanup(instance, instance.Script.InvokeOnDisable, "OnDisable");
        TryCleanup(instance, instance.Script.InvokeOnDestroy, "OnDestroy");
        ManagedEventRuntime.RemoveOwner(entity);
    }

    private static void TryCleanup(ScriptInstance instance, Action callback, string method)
    {
        try { ManagedEventRuntime.InvokeFor(instance.Script.Entity.Id, callback); }
        catch (Exception error) { Log.Error($"C# {instance.Script.GetType().FullName}.{method} cleanup failed: {error}"); }
    }

    private static void ClearNativeApis()
    {
        NativeExtension.Address = 0;
        NativeApi.Clear();
    }
}
