namespace TotkSync.Core.Settings;

public sealed class InvalidSettingsException(string message) : Exception(message);
