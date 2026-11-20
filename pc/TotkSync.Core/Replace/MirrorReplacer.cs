using TotkSync.Core.Game;
using TotkSync.Core.Saves;
using TotkSync.Core.Zip;

namespace TotkSync.Core.Replace;

public sealed class MirrorReplacer(IFileOps ops, IGameProcess game, SwapRetry? retry = null)
{
    private readonly SwapRetry _retry = retry ?? SwapRetry.Default;

    public ReplaceResult Replace(string saveDir, ValidatedSaveZip zip)
    {
        var names = StagingNames.For(saveDir);
        try
        {
            return ReplaceWith(saveDir, names, zip);
        }
        catch
        {
            RemoveQuietly(names.Stage);
            throw;
        }
    }

    private ReplaceResult ReplaceWith(string saveDir, StagingNames names, ValidatedSaveZip zip)
    {
        var stagingFailure = TryBuildStage(saveDir, names.Stage, zip);
        if (stagingFailure is not null)
        {
            RemoveQuietly(names.Stage);
            return Failed("couldn't prepare the new save", stagingFailure);
        }
        if (game.IsRunning())
        {
            RemoveQuietly(names.Stage);
            return new ReplaceResult.GameRunning();
        }
        return Swap(saveDir, names, zip);
    }

    private ReplaceResult Swap(string saveDir, StagingNames names, ValidatedSaveZip zip)
    {
        var asideFailure = Move(saveDir, names.Old);
        if (asideFailure is not null)
        {
            RemoveQuietly(names.Stage);
            return Failed("couldn't move the current save aside", asideFailure);
        }
        var installFailure = Move(names.Stage, saveDir);
        if (installFailure is not null)
        {
            return RollBack(saveDir, names, isInstalled: false, installFailure);
        }
        var verifyFailure = TryVerify(saveDir, zip);
        if (verifyFailure is not null)
        {
            return RollBack(saveDir, names, isInstalled: true, verifyFailure);
        }
        RemoveQuietly(names.Old);
        return new ReplaceResult.Replaced();
    }

    private ReplaceResult RollBack(string saveDir, StagingNames names, bool isInstalled, Exception cause)
    {
        var undoFailure = (isInstalled ? RemoveInstalled(saveDir, names) : null) ?? Move(names.Old, saveDir);
        if (undoFailure is not null)
        {
            MarkUnverified(names.Old, isInstalled);
            return new ReplaceResult.Failed(
                $"couldn't install the new save ({cause.Message}); restoring the previous save also failed ({undoFailure.Message}), it is kept at {names.Old}");
        }
        RemoveQuietly(names.Stage);
        return Failed("couldn't install the new save", cause);
    }

    private Exception? RemoveInstalled(string saveDir, StagingNames names)
    {
        var moveFailure = Move(saveDir, names.Stage);
        return moveFailure is null ? null : _retry.Run(() => ops.DeleteDirectory(saveDir));
    }

    private void MarkUnverified(string oldFolder, bool isInstalled)
    {
        if (isInstalled)
        {
            _ = _retry.Run(() => ops.WriteFile(StagingNames.UnverifiedMarkerFor(oldFolder), []));
        }
    }

    private Exception? Move(string from, string to)
    {
        var failure = _retry.Run(() => ops.MoveDirectory(from, to));
        return failure is not null && HasMoved(from, to) ? null : failure;
    }

    private static bool HasMoved(string from, string to) => !Directory.Exists(from) && Directory.Exists(to);

    private Exception? TryBuildStage(string saveDir, string stage, ValidatedSaveZip zip)
    {
        try
        {
            ops.CreateDirectory(stage);
            zip.Directories.ToList().ForEach(directory => EnsureDirectory(InsideStage(stage, directory)));
            zip.Files.ToList().ForEach(file => WriteStageFile(stage, file));
            CopyJunk(saveDir, stage);
            VerifyMatches(stage, zip);
            return null;
        }
        catch (Exception failure)
        {
            return failure;
        }
    }

    private void WriteStageFile(string stage, SaveZipFile file)
    {
        var target = InsideStage(stage, file.RelPath);
        EnsureDirectory(Path.GetDirectoryName(target)!);
        ops.WriteFile(target, file.Content);
    }

    private void CopyJunk(string saveDir, string stage)
    {
        foreach (var junk in Directory.EnumerateFiles(saveDir, "*", SearchOption.AllDirectories))
        {
            var relPath = Path.GetRelativePath(saveDir, junk);
            if (!JunkFilter.IsJunk(relPath))
            {
                continue;
            }
            var target = InsideStage(stage, relPath);
            EnsureDirectory(Path.GetDirectoryName(target)!);
            ops.CopyFile(junk, target);
        }
    }

    private void EnsureDirectory(string path)
    {
        if (!Directory.Exists(path))
        {
            ops.CreateDirectory(path);
        }
    }

    private static string InsideStage(string stage, string relPath)
    {
        var root = Path.GetFullPath(stage) + Path.DirectorySeparatorChar;
        var full = Path.GetFullPath(Path.Combine(stage, relPath));
        return full.StartsWith(root, StringComparison.OrdinalIgnoreCase)
            ? full
            : throw new IOException($"path leaves the staging folder: {relPath}");
    }

    private static Exception? TryVerify(string dir, ValidatedSaveZip zip)
    {
        try
        {
            VerifyMatches(dir, zip);
            return null;
        }
        catch (Exception failure)
        {
            return failure;
        }
    }

    private static void VerifyMatches(string dir, ValidatedSaveZip zip)
    {
        var isSameFiles = Fingerprint.OfDirectory(dir) == Fingerprint.OfZip(zip);
        var hasAllDirectories = zip.Directories.All(directory => Directory.Exists(Path.Combine(dir, directory)));
        if (!isSameFiles || !hasAllDirectories)
        {
            throw new IOException("the written save doesn't match the ZIP");
        }
    }

    private void RemoveQuietly(string directory)
    {
        _ = _retry.Run(() => ops.DeleteDirectory(directory));
    }

    private static ReplaceResult.Failed Failed(string step, Exception cause)
    {
        return new ReplaceResult.Failed($"{step}: {cause.Message}");
    }
}
