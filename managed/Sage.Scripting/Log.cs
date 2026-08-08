namespace Sage;

public static class Log
{
    public static void Info(string message) => NativeApi.Log(LogLevel.Info, message);
    public static void Warning(string message) => NativeApi.Log(LogLevel.Warning, message);
    public static void Error(string message) => NativeApi.Log(LogLevel.Error, message);
}
