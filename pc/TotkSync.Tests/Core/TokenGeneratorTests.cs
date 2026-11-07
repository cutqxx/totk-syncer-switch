using System.Text.RegularExpressions;
using TotkSync.Core.Settings;

namespace TotkSync.Tests.Core;

public class TokenGeneratorTests
{
    [Fact]
    public void Token_is_64_lowercase_hex_characters_which_is_256_bits()
    {
        Assert.Matches(new Regex("^[0-9a-f]{64}$"), TokenGenerator.Generate());
    }

    [Fact]
    public void Each_generated_token_is_different()
    {
        var tokens = Enumerable.Range(0, 200).Select(_ => TokenGenerator.Generate()).ToHashSet();

        Assert.Equal(200, tokens.Count);
    }
}
