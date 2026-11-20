using TotkSync.Core.Replace;
using TotkSync.Core.Saves;
using TotkSync.Core.Zip;
using TotkSync.Tests.Support;

namespace TotkSync.Tests.Core;

public sealed class MirrorReplacerTests : IAsyncLifetime
{
    private ReceiverRig _rig = null!;

    public async Task InitializeAsync() => _rig = await ReceiverRig.CreateAsync();

    public Task DisposeAsync()
    {
        _rig.Dispose();
        return Task.CompletedTask;
    }

    private static ValidatedSaveZip NewSave(params (string Name, string Content)[] extra)
    {
        return ZipBuilder.Valid(ZipBuilder.With(
            [("slot_01/progress.sav", "NEW slot1"), ("picturebook/a.dat", "NEW a"), .. extra]));
    }

    private ReplaceResult Replace(ValidatedSaveZip zip) => _rig.Replacer.Replace(_rig.SaveDir, zip);

    private string Read(string relPath) => File.ReadAllText(Path.Combine(_rig.SaveDir, relPath));

    [Fact]
    public void Replacing_gives_replaced_and_the_save_fingerprint_equals_the_zip_fingerprint()
    {
        var zip = NewSave();

        var result = Replace(zip);

        Assert.IsType<ReplaceResult.Replaced>(result);
        Assert.Equal(Fingerprint.OfZip(zip), Fingerprint.OfDirectory(_rig.SaveDir));
    }

    [Fact]
    public void Every_file_in_the_save_matches_the_zip_content_byte_for_byte()
    {
        var zip = NewSave(("storage/blob.bin", "binary \u0000ÿ payload"));

        Replace(zip);

        Assert.All(zip.Files, file => Assert.Equal(file.Content, File.ReadAllBytes(Path.Combine(_rig.SaveDir, file.RelPath))));
    }

    [Fact]
    public void Save_files_missing_from_the_zip_are_removed()
    {
        Replace(NewSave());

        Assert.False(File.Exists(Path.Combine(_rig.SaveDir, "slot_00/stale-only-on-pc.sav")));
        Assert.False(File.Exists(Path.Combine(_rig.SaveDir, "slot_03/progress.sav")));
    }

    [Fact]
    public void Empty_directories_of_the_zip_are_created_including_nested_ones()
    {
        Replace(NewSave(("keep-me/", ""), ("deep/er/empty/", "")));

        Assert.True(Directory.Exists(Path.Combine(_rig.SaveDir, "keep-me")));
        Assert.True(Directory.Exists(Path.Combine(_rig.SaveDir, "deep", "er", "empty")));
    }

    [Fact]
    public void Directories_that_exist_only_in_the_old_save_are_not_kept()
    {
        Replace(NewSave());

        Assert.False(Directory.Exists(Path.Combine(_rig.SaveDir, "old-empty-dir")));
    }

    [Fact]
    public void Old_junk_stays_byte_for_byte_at_its_place()
    {
        Replace(NewSave());

        Assert.Equal(["old junk", "old nested junk", "old zip junk"], [Read("Thumbs.db"), Read("slot_00/desktop.ini"), Read("album/pictures.zip")]);
    }

    [Fact]
    public void Junk_from_the_zip_is_not_written_and_does_not_replace_old_junk()
    {
        var zip = NewSave(("Thumbs.db", "zip junk"), ("slot_01/desktop.ini", "zip nested junk"), ("slot_01/extra.rar", "zip rar"));

        Replace(zip);

        Assert.Equal("old junk", Read("Thumbs.db"));
        Assert.False(File.Exists(Path.Combine(_rig.SaveDir, "slot_01/desktop.ini")));
        Assert.False(File.Exists(Path.Combine(_rig.SaveDir, "slot_01/extra.rar")));
    }

    [Fact]
    public void Jksv_meta_from_the_zip_is_never_created()
    {
        var zip = ZipBuilder.Unwrap(SaveZipValidator.Validate(ZipBuilder.ContractZip("valid-jksv-meta.zip")));

        Replace(zip);

        Assert.False(File.Exists(Path.Combine(_rig.SaveDir, JunkFilter.MetaFileName)));
        Assert.Empty(Directory.EnumerateFiles(_rig.SaveDir, JunkFilter.MetaFileName, SearchOption.AllDirectories));
    }

    [Fact]
    public void An_existing_meta_file_is_left_exactly_as_it_was_even_when_the_zip_has_another_one()
    {
        SaveTreeBuilder.WriteFile(_rig.SaveDir, JunkFilter.MetaFileName, "pc-side meta");
        var zip = ZipBuilder.Unwrap(SaveZipValidator.Validate(ZipBuilder.ContractZip("valid-jksv-meta.zip")));

        Replace(zip);

        Assert.Equal("pc-side meta", Read(JunkFilter.MetaFileName));
    }

    [Fact]
    public void Contract_save_tree_zip_replaces_the_save_with_the_contract_fingerprint()
    {
        var zip = ZipBuilder.Unwrap(SaveZipValidator.Validate(ZipBuilder.ContractZip("valid-save-tree.zip")));

        Replace(zip);

        Assert.Equal(SaveTreeBuilder.ContractFingerprint, Fingerprint.OfDirectory(_rig.SaveDir));
    }

    [Fact]
    public void Replacing_with_the_content_the_save_already_has_still_succeeds_and_changes_nothing_observable()
    {
        var same = ZipBuilder.Unwrap(SaveZipValidator.Validate(SaveZipPacker.Pack(_rig.SaveDir)));
        var before = _rig.SaveTree();

        var result = Replace(same);

        Assert.IsType<ReplaceResult.Replaced>(result);
        Assert.Equal(before, _rig.SaveTree());
    }

    [Fact]
    public void Save_path_with_a_trailing_separator_is_replaced_in_place()
    {
        var zip = NewSave();

        var result = _rig.Replacer.Replace(_rig.SaveDir + Path.DirectorySeparatorChar, zip);

        Assert.IsType<ReplaceResult.Replaced>(result);
        Assert.Equal(Fingerprint.OfZip(zip), Fingerprint.OfDirectory(_rig.SaveDir));
    }

    [Fact]
    public void Read_only_files_in_the_old_save_do_not_block_the_replacement()
    {
        File.SetAttributes(Path.Combine(_rig.SaveDir, "option.sav"), FileAttributes.ReadOnly);
        var zip = NewSave();

        var result = Replace(zip);

        Assert.IsType<ReplaceResult.Replaced>(result);
        Assert.Empty(TreeSnapshot.Leftovers(_rig.ProfileDir));
    }

    [Fact]
    public void Success_leaves_no_stage_or_old_folder_and_no_neighbour_changes()
    {
        var neighbours = _rig.Neighbours();

        Replace(NewSave());

        Assert.Empty(TreeSnapshot.Leftovers(_rig.GameDir));
        Assert.Equal(neighbours, _rig.Neighbours());
    }

    [Fact]
    public void Stage_and_old_folders_are_created_next_to_the_save_never_inside_it()
    {
        var created = new List<string>();
        _rig.Ops.OnBefore = call =>
        {
            if (call.Op == FileOp.MoveDirectory)
            {
                created.Add(call.Target!);
            }
            if (call.Op == FileOp.CreateDirectory && StagingNames.IsLeftover(call.Path))
            {
                created.Add(call.Path);
            }
        };

        Replace(NewSave());

        Assert.All(created.Where(StagingNames.IsLeftover), path => Assert.Equal(_rig.ProfileDir, Path.GetDirectoryName(path)));
        Assert.NotEmpty(created);
    }

    [Fact]
    public void Running_game_at_the_start_gives_game_running_and_changes_nothing()
    {
        _rig.Game.Running = true;
        var before = _rig.SaveTree();

        var result = Replace(NewSave());

        Assert.IsType<ReplaceResult.GameRunning>(result);
        Assert.Equal(before, _rig.SaveTree());
        Assert.Empty(TreeSnapshot.Leftovers(_rig.GameDir));
    }

    [Fact]
    public void Game_started_while_the_stage_was_being_written_stops_before_the_swap()
    {
        _rig.Ops.OnAfter = call =>
        {
            if (call.Op == FileOp.WriteFile)
            {
                _rig.Game.Running = true;
            }
        };
        var before = _rig.SaveTree();

        var result = Replace(NewSave());

        Assert.IsType<ReplaceResult.GameRunning>(result);
        Assert.Equal(before, _rig.SaveTree());
        Assert.Empty(TreeSnapshot.Leftovers(_rig.GameDir));
        Assert.Equal(0, _rig.Ops.CountOf(FileOp.MoveDirectory));
    }

    [Fact]
    public void While_the_stage_is_being_written_the_real_save_is_still_the_old_one()
    {
        var oldFingerprint = Fingerprint.OfDirectory(_rig.SaveDir);
        string? seenWhileWriting = null;
        _rig.Ops.OnBefore = call =>
        {
            if (call.Op == FileOp.WriteFile && seenWhileWriting is null)
            {
                seenWhileWriting = Fingerprint.OfDirectory(_rig.SaveDir);
            }
        };

        Replace(NewSave());

        Assert.Equal(oldFingerprint, seenWhileWriting);
    }
}
