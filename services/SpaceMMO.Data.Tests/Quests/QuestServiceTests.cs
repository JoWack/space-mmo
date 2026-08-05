using Microsoft.EntityFrameworkCore;
using SpaceMMO.Data.Entities;
using SpaceMMO.Data.Quests;
using SpaceMMO.Domain.Characters;
using SpaceMMO.Domain.Economy;
using SpaceMMO.Domain.Progression;
using SpaceMMO.Domain.Quests;
using SpaceMMO.Domain.Universe;
using Xunit;

namespace SpaceMMO.Data.Tests.Quests;

/// <summary>
/// Integration tests for the quest engine, built on the real onboarding chain from
/// design-bible §4.
/// </summary>
/// <remarks>
/// The cap tests matter most. Story rewards must bypass the daily faucet cap or a new player's
/// 13,000-credit tutorial would be throttled across three days; repeatable sidequests must respect
/// it or the steady-state faucet is unbounded.
/// </remarks>
[Collection(SharedDatabase.Name)]
public sealed class QuestServiceTests(DatabaseFixture fixture) : IAsyncLifetime
{
    private readonly DatabaseFixture _fixture = fixture;

    private const int StartingCredits = 0;

    private int _characterId;

    public async Task InitializeAsync()
    {
        await _fixture.ResetAsync();
        await SeedAsync();
    }

    public Task DisposeAsync() => Task.CompletedTask;

    private static Credits Cr(long whole) => Credits.FromWholeCredits(whole);

    private static ObjectiveEvent Gathered(string key, int quantity) =>
        new(ObjectiveType.Gather, key, quantity);

    // ── Accepting ────────────────────────────────────────────────────────────

    [Fact]
    public async Task AcceptingAQuest_StartsItAtTheFirstStep()
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        long id = await new QuestService(context).AcceptAsync(_characterId, "intro_gather_scrap");

        await using SpaceMmoDbContext verify = _fixture.CreateContext();
        CharacterQuest quest = await verify.CharacterQuests.SingleAsync(q => q.Id == id);

        Assert.Equal(QuestState.InProgress, quest.State);
        Assert.Equal(1, quest.StepOrdinal);
        Assert.Equal(0, quest.StepProgress);
    }

    [Fact]
    public async Task AcceptingTheSameQuestTwice_IsRejected()
    {
        await using SpaceMmoDbContext first = _fixture.CreateContext();
        await new QuestService(first).AcceptAsync(_characterId, "intro_gather_scrap");

        await using SpaceMmoDbContext second = _fixture.CreateContext();

        await Assert.ThrowsAsync<InvalidOperationException>(() =>
            new QuestService(second).AcceptAsync(_characterId, "intro_gather_scrap"));
    }

    [Fact]
    public async Task AcceptingALockedQuest_IsRejected()
    {
        // The second quest requires the first, so the chain cannot be skipped.
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        QuestLockedException error = await Assert.ThrowsAsync<QuestLockedException>(() =>
            new QuestService(context).AcceptAsync(_characterId, "intro_craft_tool"));

        Assert.Equal("intro_gather_scrap", error.PrerequisiteKey);
    }

    [Fact]
    public async Task AQuestUnlocks_OnceItsPrerequisiteIsComplete()
    {
        await CompleteGatherScrapAsync();

        await using SpaceMmoDbContext context = _fixture.CreateContext();

        long id = await new QuestService(context).AcceptAsync(_characterId, "intro_craft_tool");

        Assert.True(id > 0);
    }

    // ── Progress ─────────────────────────────────────────────────────────────

    [Fact]
    public async Task ProgressAdvancesTheCurrentStep()
    {
        await using SpaceMmoDbContext accept = _fixture.CreateContext();
        long id = await new QuestService(accept).AcceptAsync(_characterId, "intro_gather_scrap");

        await using SpaceMmoDbContext context = _fixture.CreateContext();
        RecordProgressResult result = await new QuestService(context)
            .RecordProgressAsync(_characterId, Gathered("scrap_alloy", 4));

        Assert.Contains(id, result.QuestsAdvanced);
        Assert.Empty(result.QuestsCompleted);

        await using SpaceMmoDbContext verify = _fixture.CreateContext();
        Assert.Equal(4, (await verify.CharacterQuests.SingleAsync(q => q.Id == id)).StepProgress);
    }

    // ── Turn-in ──────────────────────────────────────────────────────────────

    [Fact]
    public async Task AQuestRequiringTurnIn_FinishesUnpaid()
    {
        long id = await SeedTurnInQuestAsync();

        await using SpaceMmoDbContext accept = _fixture.CreateContext();
        await new QuestService(accept).AcceptAsync(_characterId, "npc_errand");

        await using SpaceMmoDbContext context = _fixture.CreateContext();
        RecordProgressResult result = await new QuestService(context)
            .RecordProgressAsync(_characterId, Gathered("scrap_alloy", 5));

        // The work is done and the money is not paid. That gap is the whole point: an NPC quest
        // has to survive the walk back to whoever gave it.
        Assert.True(result.CreditsGranted.IsZero);

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        CharacterQuest stored = await verify.CharacterQuests.SingleAsync(q => q.QuestDefId == id);

        Assert.Equal(QuestState.ReadyToTurnIn, stored.State);
        Assert.True((await verify.Characters.SingleAsync(c => c.Id == _characterId)).Balance.IsZero);
    }

    [Fact]
    public async Task TurningIn_PaysTheReward()
    {
        await SeedTurnInQuestAsync();

        await using SpaceMmoDbContext accept = _fixture.CreateContext();
        await new QuestService(accept).AcceptAsync(_characterId, "npc_errand");

        await using SpaceMmoDbContext progress = _fixture.CreateContext();
        await new QuestService(progress)
            .RecordProgressAsync(_characterId, Gathered("scrap_alloy", 5));

        await using SpaceMmoDbContext context = _fixture.CreateContext();
        FaucetGrant reward = await new QuestService(context).TurnInAsync(_characterId, "npc_errand");

        Assert.Equal(Credits.FromWholeCredits(300), reward.Granted);

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        Assert.Equal(
            QuestState.Completed,
            (await verify.CharacterQuests.SingleAsync(q => q.QuestDef!.Key == "npc_errand")).State);

        Assert.Equal(
            Credits.FromWholeCredits(300),
            (await verify.Characters.SingleAsync(c => c.Id == _characterId)).Balance);
    }

    [Fact]
    public async Task TurningInTwice_IsRefused()
    {
        await SeedTurnInQuestAsync();

        await using SpaceMmoDbContext accept = _fixture.CreateContext();
        await new QuestService(accept).AcceptAsync(_characterId, "npc_errand");

        await using SpaceMmoDbContext progress = _fixture.CreateContext();
        await new QuestService(progress)
            .RecordProgressAsync(_characterId, Gathered("scrap_alloy", 5));

        await using SpaceMmoDbContext first = _fixture.CreateContext();
        await new QuestService(first).TurnInAsync(_characterId, "npc_errand");

        // Refused rather than silently ignored. A second turn-in means something upstream believes
        // it is owed a second payment, and paying it twice is the kind of bug a player finds first.
        await using SpaceMmoDbContext second = _fixture.CreateContext();

        await Assert.ThrowsAsync<InvalidOperationException>(
            () => new QuestService(second).TurnInAsync(_characterId, "npc_errand"));

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        Assert.Equal(
            Credits.FromWholeCredits(300),
            (await verify.Characters.SingleAsync(c => c.Id == _characterId)).Balance);
    }

    [Fact]
    public async Task TurningInAnUnfinishedQuest_IsRefused()
    {
        await SeedTurnInQuestAsync();

        await using SpaceMmoDbContext accept = _fixture.CreateContext();
        await new QuestService(accept).AcceptAsync(_characterId, "npc_errand");

        // Only a quest the server itself moved to ReadyToTurnIn can be handed in, which is what
        // keeps this from becoming the "complete this quest" call the design forbids.
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        await Assert.ThrowsAsync<InvalidOperationException>(
            () => new QuestService(context).TurnInAsync(_characterId, "npc_errand"));
    }

    /// <summary>A one-step quest that must be handed in, paying 300 credits.</summary>
    private async Task<int> SeedTurnInQuestAsync()
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        var quest = new QuestDef
        {
            Key = "npc_errand",
            Name = "An Errand",
            Kind = QuestKind.MainStory,
            RewardCredits = Credits.FromWholeCredits(300),
            RequiresTurnIn = true,
        };

        context.QuestDefs.Add(quest);
        await context.SaveChangesAsync();

        context.QuestSteps.Add(new QuestStep
        {
            QuestDefId = quest.Id,
            Ordinal = 1,
            ObjectiveType = ObjectiveType.Gather,
            TargetKey = "scrap_alloy",
            Quantity = 5,
            Description = "Collect scrap.",
        });

        await context.SaveChangesAsync();

        return quest.Id;
    }

    [Fact]
    public async Task UnrelatedProgress_IsIgnored()
    {
        await using SpaceMmoDbContext accept = _fixture.CreateContext();
        await new QuestService(accept).AcceptAsync(_characterId, "intro_gather_scrap");

        await using SpaceMmoDbContext context = _fixture.CreateContext();
        RecordProgressResult result = await new QuestService(context)
            .RecordProgressAsync(_characterId, Gathered("ferrite_ore", 50));

        Assert.Empty(result.QuestsAdvanced);
    }

    [Fact]
    public async Task ProgressWithNoActiveQuests_IsHarmless()
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        RecordProgressResult result = await new QuestService(context)
            .RecordProgressAsync(_characterId, Gathered("scrap_alloy", 10));

        Assert.Empty(result.QuestsAdvanced);
        Assert.True(result.CreditsGranted.IsZero);
    }

    [Fact]
    public async Task CompletingAStep_AdvancesToTheNext()
    {
        // The two-step sidequest, so there is a next step to advance into.
        await using SpaceMmoDbContext accept = _fixture.CreateContext();
        long id = await new QuestService(accept).AcceptAsync(_characterId, "daily_haul");

        await using SpaceMmoDbContext context = _fixture.CreateContext();
        await new QuestService(context).RecordProgressAsync(_characterId, Gathered("scrap_alloy", 5));

        await using SpaceMmoDbContext verify = _fixture.CreateContext();
        CharacterQuest quest = await verify.CharacterQuests.SingleAsync(q => q.Id == id);

        Assert.Equal(2, quest.StepOrdinal);

        // Progress resets, so the second step starts from zero rather than inheriting surplus.
        Assert.Equal(0, quest.StepProgress);
        Assert.Equal(QuestState.InProgress, quest.State);
    }

    [Fact]
    public async Task OneEvent_AdvancesEveryQuestThatWantsIt()
    {
        // Two quests can legitimately ask for the same action, and a player should not have to
        // guess which one their gathering counts toward.
        await using (SpaceMmoDbContext accept = _fixture.CreateContext())
        {
            var service = new QuestService(accept);
            await service.AcceptAsync(_characterId, "intro_gather_scrap");
            await service.AcceptAsync(_characterId, "daily_haul");
        }

        await using SpaceMmoDbContext context = _fixture.CreateContext();
        RecordProgressResult result = await new QuestService(context)
            .RecordProgressAsync(_characterId, Gathered("scrap_alloy", 5));

        Assert.Equal(2, result.QuestsAdvanced.Count);
    }

    // ── Completion and rewards ───────────────────────────────────────────────

    [Fact]
    public async Task CompletingAStoryQuest_PaysCreditsAndXp()
    {
        await using SpaceMmoDbContext accept = _fixture.CreateContext();
        long id = await new QuestService(accept).AcceptAsync(_characterId, "intro_gather_scrap");

        await using SpaceMmoDbContext context = _fixture.CreateContext();
        RecordProgressResult result = await new QuestService(context)
            .RecordProgressAsync(_characterId, Gathered("scrap_alloy", 10));

        Assert.Contains(id, result.QuestsCompleted);
        Assert.Equal(Cr(500), result.CreditsGranted);

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        Assert.Equal(Cr(500), (await verify.Characters.SingleAsync()).Balance);
        Assert.Equal(200, (await verify.CharacterSkills.SingleAsync()).Xp);
        Assert.Equal(
            QuestState.Completed,
            (await verify.CharacterQuests.SingleAsync(q => q.Id == id)).State);
    }

    [Fact]
    public async Task StoryRewards_AreRecordedAsAnUncappedFaucet()
    {
        await CompleteGatherScrapAsync();

        await using SpaceMmoDbContext verify = _fixture.CreateContext();
        LedgerEntry entry = await verify.LedgerEntries.SingleAsync();

        Assert.Equal(LedgerReason.StoryReward, entry.Reason);
        Assert.False(LedgerReasons.IsCappedFaucet(entry.Reason));
    }

    [Fact]
    public async Task StoryRewards_BypassTheDailyCap()
    {
        // The whole onboarding chain pays 13,000 credits against a 5,000 daily cap. If story
        // rewards were capped, a new player's tutorial would stall for two days.
        await CompleteGatherScrapAsync();          // 500
        await CompleteQuestAsync("intro_craft_tool", ObjectiveType.Craft, "crude_mining_laser", 1);
        await CompleteQuestAsync("intro_mine_ore", ObjectiveType.Gather, "ferrite_ore", 20);

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        // 500 + 750 + 1,000, none of it withheld.
        Assert.Equal(Cr(2_250), (await verify.Characters.SingleAsync()).Balance);

        // And nothing was booked against the daily budget.
        Assert.Equal(0, await verify.CharacterFaucetDailies.CountAsync());
    }

    [Fact]
    public async Task SidequestRewards_AreCappedAndRecordedAgainstTheDailyBudget()
    {
        await CompleteDailyHaulAsync();

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        LedgerEntry entry = await verify.LedgerEntries.SingleAsync();
        Assert.Equal(LedgerReason.QuestReward, entry.Reason);
        Assert.True(LedgerReasons.IsCappedFaucet(entry.Reason));

        CharacterFaucetDaily daily = await verify.CharacterFaucetDailies.SingleAsync();
        Assert.Equal(Cr(2_000), daily.CreditsGranted);
    }

    [Fact]
    public async Task SidequestRewards_AreWithheldOnceTheDailyCapIsReached()
    {
        // Three runs at 2,000 fit inside the 5,000 cap only partially: the third is clipped.
        await CompleteDailyHaulAsync();
        await ClearCooldownAsync("daily_haul");
        await CompleteDailyHaulAsync();
        await ClearCooldownAsync("daily_haul");

        RecordProgressResult third = await CompleteDailyHaulAsync();

        Assert.Equal(Cr(1_000), third.CreditsGranted);
        Assert.Equal(Cr(1_000), third.CreditsWithheld);

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        Assert.Equal(Cr(5_000), (await verify.Characters.SingleAsync()).Balance);
        Assert.Equal(Cr(5_000), (await verify.CharacterFaucetDailies.SingleAsync()).CreditsGranted);
    }

    [Fact]
    public async Task ReachingTheCap_StillAwardsXp()
    {
        // Withholding only credits keeps the content worth doing for progression. A hard wall
        // would teach players to stop playing at the cap.
        for (int i = 0; i < 3; i++)
        {
            await CompleteDailyHaulAsync();
            await ClearCooldownAsync("daily_haul");
        }

        RecordProgressResult capped = await CompleteDailyHaulAsync();

        Assert.True(capped.CreditsGranted.IsZero);

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        // Four completions at 50 XP each, entirely unaffected by the credit cap.
        Assert.Equal(200, (await verify.CharacterSkills.SingleAsync()).Xp);
    }

    // ── Repeatability ────────────────────────────────────────────────────────

    [Fact]
    public async Task RetakingAOneShotQuest_IsRejected()
    {
        await CompleteGatherScrapAsync();

        await using SpaceMmoDbContext context = _fixture.CreateContext();

        await Assert.ThrowsAsync<InvalidOperationException>(() =>
            new QuestService(context).AcceptAsync(_characterId, "intro_gather_scrap"));
    }

    [Fact]
    public async Task RetakingARepeatableQuestTooSoon_IsRejected()
    {
        await CompleteDailyHaulAsync();

        await using SpaceMmoDbContext context = _fixture.CreateContext();

        QuestOnCooldownException error = await Assert.ThrowsAsync<QuestOnCooldownException>(() =>
            new QuestService(context).AcceptAsync(_characterId, "daily_haul"));

        Assert.Equal("daily_haul", error.QuestKey);
    }

    [Fact]
    public async Task ARepeatableQuest_CanBeRetakenAfterItsCooldown()
    {
        await CompleteDailyHaulAsync();
        await ClearCooldownAsync("daily_haul");

        await using SpaceMmoDbContext context = _fixture.CreateContext();

        long id = await new QuestService(context).AcceptAsync(_characterId, "daily_haul");

        Assert.True(id > 0);
    }

    [Fact]
    public async Task CompletionHistory_IsKeptForRepeatables()
    {
        // One row per completion, which is what the cooldown check reads.
        await CompleteDailyHaulAsync();
        await ClearCooldownAsync("daily_haul");
        await CompleteDailyHaulAsync();

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        Assert.Equal(2, await verify.CharacterQuests.CountAsync(q => q.State == QuestState.Completed));
    }

    // ── Helpers ──────────────────────────────────────────────────────────────

    private async Task CompleteGatherScrapAsync() =>
        await CompleteQuestAsync("intro_gather_scrap", ObjectiveType.Gather, "scrap_alloy", 10);

    private async Task<RecordProgressResult> CompleteQuestAsync(
        string questKey, ObjectiveType type, string targetKey, int quantity)
    {
        await using (SpaceMmoDbContext accept = _fixture.CreateContext())
        {
            await new QuestService(accept).AcceptAsync(_characterId, questKey);
        }

        await using SpaceMmoDbContext context = _fixture.CreateContext();

        return await new QuestService(context)
            .RecordProgressAsync(_characterId, new ObjectiveEvent(type, targetKey, quantity));
    }

    /// <summary>Completes the two-step repeatable sidequest.</summary>
    private async Task<RecordProgressResult> CompleteDailyHaulAsync()
    {
        await using (SpaceMmoDbContext accept = _fixture.CreateContext())
        {
            await new QuestService(accept).AcceptAsync(_characterId, "daily_haul");
        }

        await using (SpaceMmoDbContext first = _fixture.CreateContext())
        {
            await new QuestService(first)
                .RecordProgressAsync(_characterId, Gathered("scrap_alloy", 5));
        }

        await using SpaceMmoDbContext second = _fixture.CreateContext();

        return await new QuestService(second)
            .RecordProgressAsync(_characterId, Gathered("ferrite_ore", 5));
    }

    /// <summary>Backdates a completion so the cooldown has elapsed.</summary>
    private async Task ClearCooldownAsync(string questKey)
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        int questDefId = await context.QuestDefs
            .Where(q => q.Key == questKey)
            .Select(q => q.Id)
            .SingleAsync();

        List<CharacterQuest> completions = await context.CharacterQuests
            .Where(q => q.QuestDefId == questDefId && q.State == QuestState.Completed)
            .ToListAsync();

        foreach (CharacterQuest completion in completions)
        {
            completion.CompletedAt = DateTimeOffset.UtcNow.AddDays(-1);
        }

        await context.SaveChangesAsync();
    }

    private async Task SeedAsync()
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        var system = new StarSystem
        {
            Key = "system_origin",
            Name = "Origin",
            Seed = 42,
            GeneratorVersion = 1,
            SecurityLevel = SecurityLevel.Secure,
        };
        context.StarSystems.Add(system);
        await context.SaveChangesAsync();

        var body = new Body
        {
            Key = "body_terra",
            Name = "Terra",
            StarSystemId = system.Id,
            Kind = BodyKind.Planet,
            SecurityLevel = SecurityLevel.Secure,
            RadiusKm = 637.1,
        };
        context.Bodies.Add(body);

        var gathering = new Skill
        {
            Key = "gathering", Name = "Gathering", Category = SkillCategory.Life,
        };
        context.Skills.Add(gathering);

        var account = new Account
        {
            Email = "quests@example.com",
            PasswordHash = "not-a-real-hash",
            CreatedAt = DateTimeOffset.UtcNow,
        };
        context.Accounts.Add(account);
        await context.SaveChangesAsync();

        var character = new Character
        {
            AccountId = account.Id,
            Name = "Newcomer",
            Race = Race.Humanoid,
            HomeBodyId = body.Id,
            Balance = Cr(StartingCredits),
            CreatedAt = DateTimeOffset.UtcNow,
        };
        context.Characters.Add(character);
        await context.SaveChangesAsync();

        // The first three steps of the real onboarding chain, design-bible §4.
        var gatherScrap = new QuestDef
        {
            Key = "intro_gather_scrap",
            Name = "Salvage Rights",
            Kind = QuestKind.MainStory,
            RewardCredits = Cr(500),
            RewardSkillId = gathering.Id,
            RewardXp = 200,
        };
        context.QuestDefs.Add(gatherScrap);
        await context.SaveChangesAsync();

        var craftTool = new QuestDef
        {
            Key = "intro_craft_tool",
            Name = "First Tools",
            Kind = QuestKind.MainStory,
            PrerequisiteQuestDefId = gatherScrap.Id,
            RewardCredits = Cr(750),
        };
        context.QuestDefs.Add(craftTool);
        await context.SaveChangesAsync();

        var mineOre = new QuestDef
        {
            Key = "intro_mine_ore",
            Name = "Into the Rock",
            Kind = QuestKind.MainStory,
            PrerequisiteQuestDefId = craftTool.Id,
            RewardCredits = Cr(1_000),
        };

        // A repeatable sidequest, to exercise the cooldown and the daily credit cap.
        var dailyHaul = new QuestDef
        {
            Key = "daily_haul",
            Name = "Supply Run",
            Kind = QuestKind.Sidequest,
            RewardCredits = Cr(2_000),
            RewardSkillId = gathering.Id,
            RewardXp = 50,
            CooldownSeconds = 3_600,
        };

        context.QuestDefs.AddRange(mineOre, dailyHaul);
        await context.SaveChangesAsync();

        context.QuestSteps.AddRange(
            new QuestStep
            {
                QuestDefId = gatherScrap.Id, Ordinal = 1,
                Description = "Gather 10 scrap alloy from surface debris.",
                ObjectiveType = ObjectiveType.Gather, TargetKey = "scrap_alloy", Quantity = 10,
            },
            new QuestStep
            {
                QuestDefId = craftTool.Id, Ordinal = 1,
                Description = "Craft a crude mining laser.",
                ObjectiveType = ObjectiveType.Craft, TargetKey = "crude_mining_laser", Quantity = 1,
            },
            new QuestStep
            {
                QuestDefId = mineOre.Id, Ordinal = 1,
                Description = "Mine 20 ferrite ore.",
                ObjectiveType = ObjectiveType.Gather, TargetKey = "ferrite_ore", Quantity = 20,
            },
            new QuestStep
            {
                QuestDefId = dailyHaul.Id, Ordinal = 1,
                Description = "Collect 5 scrap alloy.",
                ObjectiveType = ObjectiveType.Gather, TargetKey = "scrap_alloy", Quantity = 5,
            },
            new QuestStep
            {
                QuestDefId = dailyHaul.Id, Ordinal = 2,
                Description = "Collect 5 ferrite ore.",
                ObjectiveType = ObjectiveType.Gather, TargetKey = "ferrite_ore", Quantity = 5,
            });

        await context.SaveChangesAsync();

        _characterId = character.Id;
    }
}
