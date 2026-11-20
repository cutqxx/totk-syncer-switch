using System.Net;
using System.Text;
using TotkSync.Server;

namespace TotkSync.Tests.Server;

public class RequestGuardTests
{
    private const string FakeToken = "fake-token-0123456789abcdef";
    private static readonly byte[] TokenBytes = Encoding.UTF8.GetBytes(FakeToken);
    private static readonly IPAddress LanClient = IPAddress.Parse("192.168.1.50");

    private static GuardResult Check(IPAddress remote, string? authorization, byte[]? token = null)
    {
        return RequestGuard.Check(remote, authorization, token ?? TokenBytes);
    }

    [Fact]
    public void Private_client_with_correct_bearer_token_is_allowed()
    {
        Assert.Equal(GuardResult.Allowed, Check(LanClient, "Bearer " + FakeToken));
    }

    [Fact]
    public void Loopback_client_with_correct_token_is_allowed()
    {
        Assert.Equal(GuardResult.Allowed, Check(IPAddress.Loopback, "Bearer " + FakeToken));
    }

    [Fact]
    public void Ipv4_mapped_private_client_with_correct_token_is_allowed()
    {
        Assert.Equal(GuardResult.Allowed, Check(IPAddress.Parse("::ffff:10.0.0.7"), "Bearer " + FakeToken));
    }

    [Theory]
    [InlineData("8.8.8.8")]
    [InlineData("172.15.255.255")]
    [InlineData("172.32.0.0")]
    [InlineData("::1")]
    [InlineData("2001:db8::1")]
    [InlineData("::ffff:8.8.8.8")]
    public void Non_private_client_is_forbidden_even_with_correct_token(string client)
    {
        Assert.Equal(GuardResult.Forbidden, Check(IPAddress.Parse(client), "Bearer " + FakeToken));
    }

    [Fact]
    public void Non_private_client_without_token_is_forbidden_not_unauthorized()
    {
        Assert.Equal(GuardResult.Forbidden, Check(IPAddress.Parse("8.8.8.8"), null));
    }

    [Fact]
    public void Missing_authorization_header_is_unauthorized()
    {
        Assert.Equal(GuardResult.Unauthorized, Check(LanClient, null));
    }

    [Theory]
    [InlineData("")]
    [InlineData("Bearer")]
    [InlineData("Bearer ")]
    [InlineData("Bearer wrong-token")]
    [InlineData("Bearer fake-token-0123456789abcde")]
    [InlineData("Bearer fake-token-0123456789abcdefX")]
    [InlineData("Bearer  fake-token-0123456789abcdef")]
    [InlineData("Bearer fake-token-0123456789abcdef ")]
    [InlineData("Bearer FAKE-TOKEN-0123456789ABCDEF")]
    [InlineData("Basic fake-token-0123456789abcdef")]
    [InlineData("fake-token-0123456789abcdef")]
    public void Absent_empty_wrong_or_differently_shaped_credentials_are_unauthorized(string authorization)
    {
        Assert.Equal(GuardResult.Unauthorized, Check(LanClient, authorization));
    }

    [Fact]
    public void Empty_configured_token_never_accepts_an_empty_bearer_value()
    {
        Assert.Equal(GuardResult.Unauthorized, Check(LanClient, "Bearer ", token: []));
    }

    [Fact]
    public void Empty_configured_token_rejects_any_presented_token()
    {
        Assert.Equal(GuardResult.Unauthorized, Check(LanClient, "Bearer " + FakeToken, token: []));
    }

    [Fact]
    public void Whitespace_only_configured_token_never_accepts_a_matching_bearer_value()
    {
        var blankToken = "   "u8.ToArray();

        Assert.Equal(GuardResult.Unauthorized, Check(LanClient, "Bearer    ", token: blankToken));
    }

    [Fact]
    public void Bearer_scheme_name_is_case_insensitive()
    {
        Assert.Equal(GuardResult.Allowed, Check(LanClient, "bearer " + FakeToken));
    }
}
