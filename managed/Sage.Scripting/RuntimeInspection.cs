using System.Collections;
using System.Globalization;
using System.Reflection;
using System.Runtime.CompilerServices;

namespace Sage;

/// <summary>Bounded snapshots of fields and auto-properties, without invoking gameplay getters.</summary>
internal static class RuntimeInspection
{
    private const int MaxDepth = 4;
    private const int MaxChildren = 32;
    private const int MaxNodes = 256;

    private sealed record Member(string Label, FieldInfo Field);
    // Weak keys allow collectible gameplay assemblies to unload after Stop.
    private static readonly ConditionalWeakTable<Type, Member[]> Members = new();

    private static Member[] GetMembers(Type type) => Members.GetValue(type, static type =>
    {
        var members = new List<Member>();
        for (var current = type; current != null && current != typeof(object); current = current.BaseType)
            foreach (var field in current.GetFields(BindingFlags.Instance | BindingFlags.Public |
                                                    BindingFlags.NonPublic | BindingFlags.DeclaredOnly)
                         .OrderBy(field => field.Name, StringComparer.Ordinal))
            {
                var label = field.Name;
                if (label.StartsWith('<') && label.EndsWith(">k__BackingField", StringComparison.Ordinal))
                    label = label[1..label.IndexOf('>')];
                members.Add(new(label, field));
            }
        return members.ToArray();
    });

    internal sealed record Node(string Name, string Type, string Value, List<Node> Children);

    internal static Node Capture(object instance)
    {
        var remaining = MaxNodes;
        return Read(instance.GetType().Name, instance, 0, new HashSet<object>(ReferenceEqualityComparer.Instance),
            ref remaining);
    }

    private static Node Read(string name, object? value, int depth, HashSet<object> ancestors, ref int remaining)
    {
        --remaining;
        if (value == null) return new(name, "", "null", []);
        var type = value.GetType();
        var typeName = type.Name;
        if (value is string text) return new(name, typeName, text.Length > 1024 ? text[..1024] + "…" : text, []);
        if (type.IsPrimitive || type.IsEnum || value is decimal)
            return new(name, typeName, Convert.ToString(value, CultureInfo.InvariantCulture) ?? "", []);
        if (value is Entity entity) return new(name, typeName, entity.Id.ToString(CultureInfo.InvariantCulture), []);
        if (value is Delegate || type.IsPointer || value is Subscription || value is Type)
            return new(name, typeName, typeName, []);
        if (depth >= MaxDepth || remaining <= 0) return new(name, typeName, "…", []);
        if (!type.IsValueType && !ancestors.Add(value)) return new(name, typeName, "(reference cycle)", []);

        var children = new List<Node>();
        try
        {
            // Only inspect built-in collections. Custom iterators can execute gameplay code.
            if (value is IEnumerable collection &&
                (type.IsArray || type.Assembly == typeof(List<>).Assembly && value is ICollection))
            {
                var index = 0;
                foreach (var item in collection)
                {
                    if (index >= MaxChildren || remaining <= 0) { children.Add(new("…", "", "More items", [])); break; }
                    children.Add(Read($"[{index++}]", item, depth + 1, ancestors, ref remaining));
                }
            }
            else
            {
                foreach (var member in GetMembers(type))
                {
                    if (children.Count >= MaxChildren || remaining <= 0)
                    {
                        children.Add(new("…", "", "More fields", []));
                        return new(name, typeName, typeName, children);
                    }
                    children.Add(Read(member.Label, member.Field.GetValue(value), depth + 1, ancestors, ref remaining));
                }
            }
        }
        catch (Exception error)
        {
            children.Add(new("Inspection error", error.GetType().Name, error.Message, []));
        }
        finally
        {
            if (!type.IsValueType) ancestors.Remove(value);
        }
        return new(name, typeName, typeName, children);
    }
}
