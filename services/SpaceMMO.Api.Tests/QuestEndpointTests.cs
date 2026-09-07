using System.Net;
using System.Net.Http.Headers;
using System.Net.Http.Json;
using SpaceMMO.Data;
using SpaceMMO.Data.Entities;
using SpaceMMO.Domain.Characters;
using SpaceMMO.Domain.Economy;
using SpaceMMO.Domain.Quests;
using Xunit;

namespace SpaceMMO.Api.Tests;

/// <summary>
/// Accepting a quest, and handing one in.
/// </summary>
/// <remarks>
/// <para>
/// <strong>The route existing is the claim worth making.</strong> <c>QuestService.TurnInAsync</c>
/// was written on 5 August, thoroughly tested, and never mapped — so the whole
/// <c>ReadyToTurnIn</c> state was unreachable from the game and every quest paid out the instant
/// its last objective completed (task 150). Every test of the service passed throughout, because a
/// service nobody can call still behaves correctly when called.
/// </para>
/// <para>
/// A missing route is invisible everywhere else: the client would get a 404 and quietly do
/// nothing, which is indistinguishable from a key that is not bound.
/// </para>
/// </remarks>
[Collection(SharedApiDatabase.Name)]
public sealed class QuestEndpointTests(ApiDatabaseFixture fixture) : IAsyncLifetime, IDisposable
{
    private readonly ApiDatabaseFixture _fixture = fixture;

    private ApiFactory _factory = null!;
    private HttpClient _client = null!;

    private string _token = null!;
    private string _strangersToken = null!;
    private int _characterId;

    public async Task InitializeAsync()
    {
        await _fixture.ResetAsync();

        _factory = new ApiFactory(_fixture.ConnectionString, ApiFactory.TestServiceSecret);
        _client = _factory.CreateClient();

        await AuthorizationTests.SeedStartingWorldAsync(_fixture);
        await SeedAsync();
    }

    public Task DisposeAsync() => Task.CompletedTask;

    public void Dispose()
    {
        _client?.Dispose();
        _factory?.Dispose();
    }

    /// <summary>Its own copy, like every other test class here: the API does not publish one.</summary>
    private sealed record SessionPayload(int AccountId, string Token, DateTimeOffset ExpiresAt);

    private sealed record TurnInPayload(
        string QuestKey, long GrantedMinorUnits, long WithheldMinorUnits);

    private async Task<HttpResponseMessage> PostAsync(string path, string token, object body)
    {
        var request = new HttpRequestMessage(HttpMethod.Post, new Uri(path, UriKind.Relative))
        {
            Content = JsonContent.Create(body),
        };

        request.Headers.Authorization = new AuthenticationHeaderValue("Bearer", token);

        return await _client.SendAsync(request);
    }

    [Fact]
    public async Task A_quest_whose_work_is_not_done_cannot_be_handed_in()
    {
        (await PostAsync(
            "/quests/accept", _token, new { characterId = _characterId, questKey = "errand" }))
            .EnsureSuccessStatusCode();

        HttpResponseMessage response = await PostAsync(
            "/quests/turn-in", _token, new { characterId = _characterId, questKey = "errand" });

        // A conflict rather than a not-found: the quest exists and is theirs, and "you have not
        // finished it" is a state that changes rather than a missing resource. A 404 here would
        // also be exactly what a route that does not exist returns, which is the fault this whole
        // test file is about.
        Assert.Equal(HttpStatusCode.Conflict, response.StatusCode);
    }

    [Fact]
    public async Task Finished_work_is_paid_when_it_is_handed_in_and_not_before()
    {
        (await PostAsync(
            "/quests/accept", _token, new { characterId = _characterId, questKey = "errand" }))
            .EnsureSuccessStatusCode();

        // Moved by the server, the way finishing the objectives would. The client is never allowed
        // to assert this, which is why the test writes it directly rather than posting it.
        await using (SpaceMmoDbContext context = _fixture.CreateContext())
        {
            CharacterQuest held = context.CharacterQuests.Single(q => q.CharacterId == _characterId);

            held.State = QuestState.ReadyToTurnIn;

            await context.SaveChangesAsync();
        }

        long before;

        await using (SpaceMmoDbContext balance = _fixture.CreateContext())
        {
            before = balance.Characters.Single(c => c.Id == _characterId).Balance.MinorUnits;
        }

        HttpResponseMessage response = await PostAsync(
            "/quests/turn-in", _token, new { characterId = _characterId, questKey = "errand" });

        Assert.Equal(HttpStatusCode.OK, response.StatusCode);

        TurnInPayload? paid = await response.Content.ReadFromJsonAsync<TurnInPayload>();

        Assert.Equal("errand", paid!.QuestKey);
        Assert.True(paid.GrantedMinorUnits > 0, "handing in should pay something");

        await using (SpaceMmoDbContext after = _fixture.CreateContext())
        {
            Assert.Equal(
                before + paid.GrantedMinorUnits,
                after.Characters.Single(c => c.Id == _characterId).Balance.MinorUnits);

            Assert.Equal(
                QuestState.Completed,
                after.CharacterQuests.Single(q => q.CharacterId == _characterId).State);
        }

        // Twice is a refusal, not a second payment. A client retrying a request whose answer it
        // lost must not be able to collect the reward again.
        HttpResponseMessage again = await PostAsync(
            "/quests/turn-in", _token, new { characterId = _characterId, questKey = "errand" });

        Assert.Equal(HttpStatusCode.Conflict, again.StatusCode);
    }

    [Fact]
    public async Task Somebody_elses_quest_cannot_be_handed_in()
    {
        HttpResponseMessage response = await PostAsync(
            "/quests/turn-in",
            _strangersToken,
            new { characterId = _characterId, questKey = "errand" });

        // The same refusal the rest of the character endpoints give, and it must not be a conflict:
        // that would confirm the quest exists and is unfinished to somebody with no claim on it.
        Assert.True(
            response.StatusCode is HttpStatusCode.NotFound or HttpStatusCode.Forbidden,
            $"a stranger should be refused; got {(int)response.StatusCode}");
    }

    private async Task SeedAsync()
    {
        HttpResponseMessage registered = await _client.PostAsJsonAsync(
            new Uri("/accounts/register", UriKind.Relative),
            new { email = "quester@local.test", password = "a-sufficiently-long-password" });

        registered.EnsureSuccessStatusCode();

        SessionPayload session = (await registered.Content.ReadFromJsonAsync<SessionPayload>())!;

        _token = session.Token;

        HttpResponseMessage stranger = await _client.PostAsJsonAsync(
            new Uri("/accounts/register", UriKind.Relative),
            new { email = "stranger@local.test", password = "a-sufficiently-long-password" });

        stranger.EnsureSuccessStatusCode();

        _strangersToken = (await stranger.Content.ReadFromJsonAsync<SessionPayload>())!.Token;

        await using SpaceMmoDbContext context = _fixture.CreateContext();

        Body home = context.Bodies.First();

        var character = new Character
        {
            AccountId = session.AccountId,
            Name = "Quester",
            Race = Race.Humanoid,
            HomeBodyId = home.Id,
            CreatedAt = DateTimeOffset.UtcNow,
        };

        context.Characters.Add(character);

        var errand = new QuestDef
        {
            Key = "errand",
            Name = "An Errand",
            Kind = QuestKind.MainStory,
            RewardCredits = Credits.FromWholeCredits(500),
            RequiresTurnIn = true,
        };

        context.QuestDefs.Add(errand);
        await context.SaveChangesAsync();

        context.QuestSteps.Add(new QuestStep
        {
            QuestDefId = errand.Id,
            Ordinal = 1,
            Description = "Collect ten of something.",
            ObjectiveType = ObjectiveType.Gather,
            TargetKey = "scrap_alloy",
            Quantity = 10,
        });

        await context.SaveChangesAsync();

        _characterId = character.Id;
    }
}
