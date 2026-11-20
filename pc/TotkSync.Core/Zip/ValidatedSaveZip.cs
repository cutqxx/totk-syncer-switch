namespace TotkSync.Core.Zip;

public sealed record SaveZipFile(string RelPath, byte[] Content);

public sealed record ValidatedSaveZip(IReadOnlyList<SaveZipFile> Files, IReadOnlyList<string> Directories);

public abstract record ZipValidation
{
    private ZipValidation()
    {
    }

    public sealed record Valid(ValidatedSaveZip Zip) : ZipValidation;

    public sealed record Invalid(string Reason) : ZipValidation;
}
