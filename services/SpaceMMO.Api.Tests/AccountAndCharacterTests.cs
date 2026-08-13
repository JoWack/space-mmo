using SpaceMMO.Domain.Characters;
using SpaceMMO.Domain.Progression;
using System.Net.Http.Headers;
using System.Net.Http.Json;
using System.Net;
using Xunit;

namespace SpaceMMO.Api.Tests;

/// <summary>
/// Registration, login, and character creation over real HTTP.
/// </summary>
[Collection(SharedApiDatabase.Name)]
public sealed class AccountAndCharacterTests(ApiDatabaseFixture fixture) : IAsyncLifetime, IDisposable
{
    private const string GoodPassword = "a-sufficiently-long-password";

    private readonly ApiDatabaseFixture _fixture = fixture;

    private ApiFactory _factory = null!;
    private HttpClient _client = null!;

    public async Task InitializeAsync()
    {
        await _fixture.ResetAsync();

        _factory = new ApiFactory(_fixture.ConnectionString);
        _client = _factory.CreateClient();

        await AuthorizationTests.SeedStartingWorldAsync(_fixture);
    }

    public Task DisposeAsync() => Task.CompletedTask;

    /// <summary>
    /// Disposes the client and host.
    /// </summary>
    /// <remarks>
    /// IDisposable rather than only xUnit's DisposeAsync, because CA1001 is right: a type holding
    /// an HttpClient and a web host owns them, and the analyzer cannot see that xUnit would have
    /// called the async one.
    /// </remarks>
    public void Dispose()
    {
        _client?.Dispose();
        _factory?.Dispose();
    }

    [Fact]
    public async Task Health_reports_ok()
    {
        HttpResponseMessage response =
            await _client.GetAsync(new Uri("/health", UriKind.Relative));

        Assert.Equal(HttpStatusCode.OK, response.StatusCode);
    }

    [Fact]
    public async Task Registering_then_logging_in_returns_a_working_token()
    {
        const string Email = "player@example.com";

        SessionPayload registered = await RegisterAsync(Email);

        Assert.NotEmpty(registered.Token);

        HttpResponseMessage loginResponse = await _client.PostAsJsonAsync(
            new Uri("/accounts/login", UriKind.Relative),
            new { email = Email, password = GoodPassword });

        loginResponse.EnsureSuccessStatusCode();

        SessionPayload? loggedIn = await loginResponse.Content.ReadFromJsonAsync<SessionPayload>();

        Assert.Equal(registered.AccountId, loggedIn!.AccountId);

        // The token from login must work, not merely be present.
        using var request = new HttpRequestMessage(
            HttpMethod.Get, new Uri("/characters/", UriKind.Relative));

        request.Headers.Authorization = new AuthenticationHeaderValue("Bearer", loggedIn.Token);

        Assert.Equal(HttpStatusCode.OK, (await _client.SendAsync(request)).StatusCode);
    }

    [Fact]
    public async Task Login_with_the_wrong_password_is_rejected()
    {
        const string Email = "wrongpass@example.com";

        await RegisterAsync(Email);

        HttpResponseMessage response = await _client.PostAsJsonAsync(
            new Uri("/accounts/login", UriKind.Relative),
            new { email = Email, password = "not-the-right-password" });

        Assert.Equal(HttpStatusCode.Unauthorized, response.StatusCode);
    }

    [Fact]
    public async Task Login_for_an_account_that_does_not_exist_is_rejected_the_same_way()
    {
        HttpResponseMessage response = await _client.PostAsJsonAsync(
            new Uri("/accounts/login", UriKind.Relative),
            new { email = "nobody@example.com", password = GoodPassword });

        // Identical to a wrong password. Any difference here — status, body, or timing — is an
        // account-enumeration oracle.
        Assert.Equal(HttpStatusCode.Unauthorized, response.StatusCode);
    }

    [Fact]
    public async Task A_short_password_is_refused()
    {
        HttpResponseMessage response = await _client.PostAsJsonAsync(
            new Uri("/accounts/register", UriKind.Relative),
            new { email = "short@example.com", password = "short" });

        Assert.Equal(HttpStatusCode.BadRequest, response.StatusCode);
    }

    [Fact]
    public async Task Registering_the_same_email_twice_conflicts()
    {
        await RegisterAsync("duplicate@example.com");

        HttpResponseMessage response = await _client.PostAsJsonAsync(
            new Uri("/accounts/register", UriKind.Relative),
            new { email = "duplicate@example.com", password = GoodPassword });

        Assert.Equal(HttpStatusCode.Conflict, response.StatusCode);
    }

    [Theory]
    [InlineData(Race.Humanoid, Faction.A, "body_terra")]
    [InlineData(Race.Martian, Faction.A, "body_ares")]
    [InlineData(Race.SpaceElf, Faction.B, "body_verdance")]
    [InlineData(Race.SpaceOrc, Faction.B, "body_grimhold")]
    public async Task Character_creation_derives_faction_and_home_from_race(
        Race race, Faction expectedFaction, string expectedBodyKey)
    {
        SessionPayload session = await RegisterAsync($"{race}@example.com");

        using var request = new HttpRequestMessage(
            HttpMethod.Post, new Uri("/characters/", UriKind.Relative))
        {
            Content = JsonContent.Create(new { name = $"Pilot{race}", race }),
        };

        request.Headers.Authorization = new AuthenticationHeaderValue("Bearer", session.Token);

        HttpResponseMessage response = await _client.SendAsync(request);

        Assert.Equal(HttpStatusCode.Created, response.StatusCode);

        CharacterPayload? character = await response.Content.ReadFromJsonAsync<CharacterPayload>();

        Assert.Equal(expectedFaction, character!.Faction);

        // The client never sent a faction or a body, and could not have overridden either.
        await using SpaceMMO.Data.SpaceMmoDbContext context = _fixture.CreateContext();

        string actualKey = context.Bodies.Single(b => b.Id == character.HomeBodyId).Key;

        Assert.Equal(expectedBodyKey, actualKey);
    }

    [Fact]
    public async Task A_client_cannot_choose_its_own_faction_or_starting_body()
    {
        SessionPayload session = await RegisterAsync("liar@example.com");

        using var request = new HttpRequestMessage(
            HttpMethod.Post, new Uri("/characters/", UriKind.Relative))
        {
            // A Space Orc claiming Faction A and a Terran homeworld. Both extra fields are simply
            // not bound — faction and home are computed from race, so there is nothing to override.
            Content = JsonContent.Create(new
            {
                name = "SneakyOrc",
                race = Race.SpaceOrc,
                faction = Faction.A,
                homeBodyId = 1,
            }),
        };

        request.Headers.Authorization = new AuthenticationHeaderValue("Bearer", session.Token);

        HttpResponseMessage response = await _client.SendAsync(request);
        CharacterPayload? character = await response.Content.ReadFromJsonAsync<CharacterPayload>();

        Assert.Equal(Faction.B, character!.Faction);

        await using SpaceMMO.Data.SpaceMmoDbContext context = _fixture.CreateContext();

        Assert.Equal(
            "body_grimhold",
            context.Bodies.Single(b => b.Id == character.HomeBodyId).Key);
    }

    [Fact]
    public async Task A_brand_new_character_still_reports_every_skill()
    {
        // XP rows are created lazily, on first award, so a fresh character has none at all. The
        // skills panel must still show the full catalog at level 1 — otherwise a new player opens
        // it to an empty list, which is what the first live client run actually showed.
        await SeedSkillsAsync();

        SessionPayload session = await RegisterAsync("freshskills@example.com");

        HttpResponseMessage created = await CreateCharacterAsync(session, "Rookie");
        CharacterPayload? character = await created.Content.ReadFromJsonAsync<CharacterPayload>();

        using var request = new HttpRequestMessage(
            HttpMethod.Get, new Uri($"/characters/{character!.Id}/skills", UriKind.Relative));

        request.Headers.Authorization = new AuthenticationHeaderValue("Bearer", session.Token);

        HttpResponseMessage response = await _client.SendAsync(request);

        response.EnsureSuccessStatusCode();

        List<SkillPayload>? skills = await response.Content.ReadFromJsonAsync<List<SkillPayload>>();

        Assert.Equal(3, skills!.Count);
        Assert.All(skills, s => Assert.Equal(0, s.Xp));
        Assert.All(skills, s => Assert.Equal(1, s.Level));
        Assert.Contains(skills, s => s.Key == "mining");
    }

    [Fact]
    public async Task Skills_report_progress_towards_the_next_level()
    {
        // The skills screen shows how far off the next level is, and both figures are served rather
        // than derived on the client. SkillCurve's threshold table is order-sensitive -- its own
        // comment says flooring the division and the accumulation the other way round changes some
        // levels -- so a C++ reimplementation would either reproduce that subtlety or disagree with
        // it silently, and a skill bar that disagrees with the server is worse than no bar.
        await SeedSkillsAsync();

        SessionPayload session = await RegisterAsync("progress@example.com");

        HttpResponseMessage created = await CreateCharacterAsync(session, "Apprentice");
        CharacterPayload? character = await created.Content.ReadFromJsonAsync<CharacterPayload>();

        await AwardXpAsync(character!.Id, "mining", 100L);

        using var request = new HttpRequestMessage(
            HttpMethod.Get, new Uri($"/characters/{character.Id}/skills", UriKind.Relative));

        request.Headers.Authorization = new AuthenticationHeaderValue("Bearer", session.Token);

        HttpResponseMessage response = await _client.SendAsync(request);

        response.EnsureSuccessStatusCode();

        List<SkillPayload>? skills = await response.Content.ReadFromJsonAsync<List<SkillPayload>>();

        Assert.NotNull(skills);

        SkillPayload mining = skills.Single(s => s.Key == "mining");

        // Published thresholds: level 2 begins at 83 XP and level 3 at 174. Written literally rather
        // than through SkillCurve, because computing the expectation with the same function the
        // endpoint uses would pass no matter what either of them did.
        Assert.Equal(2, mining.Level);
        Assert.Equal(74L, mining.XpToNextLevel);
        Assert.Equal((100.0 - 83.0) / (174.0 - 83.0), mining.ProgressToNextLevel, 6);

        // An untouched skill is the case a bar renders wrong most visibly: level 1, nothing done,
        // and 83 to go rather than zero.
        SkillPayload untouched = skills.Single(s => s.Key == "refining");

        Assert.Equal(1, untouched.Level);
        Assert.Equal(83L, untouched.XpToNextLevel);
        Assert.Equal(0.0, untouched.ProgressToNextLevel);
    }

    /// <summary>Gives a character XP in one skill, the way an award would.</summary>
    private async Task AwardXpAsync(int characterId, string skillKey, long xp)
    {
        await using SpaceMMO.Data.SpaceMmoDbContext context = _fixture.CreateContext();

        SpaceMMO.Data.Entities.Skill skill = context.Skills.Single(s => s.Key == skillKey);

        context.CharacterSkills.Add(new SpaceMMO.Data.Entities.CharacterSkill
        {
            CharacterId = characterId,
            SkillId = skill.Id,
            Xp = xp,
        });

        await context.SaveChangesAsync();
    }

    /// <summary>Three skills, so the endpoint has a catalog to report against.</summary>
    private async Task SeedSkillsAsync()
    {
        await using SpaceMMO.Data.SpaceMmoDbContext context = _fixture.CreateContext();

        context.Skills.AddRange(
            new SpaceMMO.Data.Entities.Skill
            {
                Key = "mining", Name = "Mining", Category = SkillCategory.Life,
            },
            new SpaceMMO.Data.Entities.Skill
            {
                Key = "refining", Name = "Refining", Category = SkillCategory.Life,
            },
            new SpaceMMO.Data.Entities.Skill
            {
                Key = "gunnery", Name = "Gunnery", Category = SkillCategory.Combat,
            });

        await context.SaveChangesAsync();
    }

    [Fact]
    public async Task Duplicate_character_names_conflict()
    {
        SessionPayload session = await RegisterAsync("namer@example.com");

        Assert.Equal(HttpStatusCode.Created, (await CreateCharacterAsync(session, "Unique")).StatusCode);
        Assert.Equal(HttpStatusCode.Conflict, (await CreateCharacterAsync(session, "Unique")).StatusCode);
    }

    private async Task<HttpResponseMessage> CreateCharacterAsync(SessionPayload session, string name)
    {
        using var request = new HttpRequestMessage(
            HttpMethod.Post, new Uri("/characters/", UriKind.Relative))
        {
            Content = JsonContent.Create(new { name, race = Race.Humanoid }),
        };

        request.Headers.Authorization = new AuthenticationHeaderValue("Bearer", session.Token);

        return await _client.SendAsync(request);
    }

    private async Task<SessionPayload> RegisterAsync(string email)
    {
        HttpResponseMessage response = await _client.PostAsJsonAsync(
            new Uri("/accounts/register", UriKind.Relative),
            new { email, password = GoodPassword });

        response.EnsureSuccessStatusCode();

        return (await response.Content.ReadFromJsonAsync<SessionPayload>())!;
    }

    private sealed record SessionPayload(int AccountId, string Token, DateTimeOffset ExpiresAt);

    private sealed record CharacterPayload(
        int Id, string Name, Race Race, Faction Faction, int HomeBodyId, long BalanceMinorUnits);

    private sealed record SkillPayload(
        string Key,
        string Name,
        SkillCategory Category,
        long Xp,
        int Level,
        long XpToNextLevel,
        double ProgressToNextLevel);
}
