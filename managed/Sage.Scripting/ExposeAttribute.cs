using System.Reflection;
using System.Text.Json;

namespace Sage;

/// <summary>A public entity field assigned through the canvas inspector.</summary>
[AttributeUsage(AttributeTargets.Field)]
public sealed class ExposeAttribute : Attribute { }

internal static class ExposedFields
{
    internal static void Apply(Script script)
    {
        var fields = script.GetType().GetFields(BindingFlags.Public | BindingFlags.Instance)
            .Where(field => field.IsDefined(typeof(ExposeAttribute))).ToArray();
        if (fields.Length == 0) return;
        var saved = script.Entity.GetComponent<ScriptFields>();
        using var document = JsonDocument.Parse(saved?.Json ?? "{}");
        foreach (var field in fields)
        {
            if (field.FieldType != typeof(Entity) || field.IsInitOnly)
                throw new InvalidOperationException($"{field.Name}: exposed fields must be writable Entity fields.");
            if (!document.RootElement.TryGetProperty(field.Name, out var value))
                throw new InvalidOperationException($"{script.GetType().Name}.{field.Name}: assign a canvas node in the inspector.");
            var entity = new Entity(value.GetUInt32());
            if (!entity.Exists) throw new InvalidOperationException($"{field.Name}: referenced entity no longer exists.");
            field.SetValue(script, entity);
        }
        foreach (var property in document.RootElement.EnumerateObject())
            if (!fields.Any(field => field.Name == property.Name))
                throw new InvalidOperationException($"{script.GetType().Name}: saved field '{property.Name}' does not exist or is not exposed.");
    }
}
