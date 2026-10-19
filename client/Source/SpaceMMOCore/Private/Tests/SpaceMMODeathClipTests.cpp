#include "Animation/AnimSequence.h"
#include "AnimationRuntime.h"
#include "BonePose.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/AutomationTest.h"
#include "SpaceMMOCharacterPawn.h"

#if WITH_DEV_AUTOMATION_TESTS

/*
 * Task 201: a body falls when its character dies, every race, away from the shot that killed it.
 */
namespace SpaceMMODeathClipTests
{
	/** Every joint at a time in the clip, in the body's component space, retargeted as the game plays it on that body. */
	TMap<FName, FVector> JointsAt(const UAnimSequence* Sequence, const USkeletalMesh* Body, const double Time)
	{
		const FReferenceSkeleton& Reference = Body->GetRefSkeleton();
		TArray<FBoneIndexType> Required;

		for (int32 Index = 0; Index < Reference.GetNum(); ++Index)
		{
			Required.Add(static_cast<FBoneIndexType>(Index));
		}

		FBoneContainer Bones;
		Bones.InitializeTo(Required, UE::Anim::FCurveFilterSettings(UE::Anim::ECurveFilterMode::DisallowAll),
			static_cast<UObject&>(*const_cast<USkeletalMesh*>(Body)));

		// Compact poses allocate from the memory stack, which wants a mark in scope.
		FMemMark Mark(FMemStack::Get());
		FCompactPose Pose;
		Pose.SetBoneContainer(&Bones);
		Pose.ResetToRefPose();
		FBlendedCurve Curve;
		Curve.InitFrom(Bones);
		UE::Anim::FStackAttributeContainer Attributes;
		FAnimationPoseData Data(Pose, Curve, Attributes);

		Sequence->GetAnimationPose(Data, FAnimExtractContext(Time, false));

		FCSPose<FCompactPose> Component;
		Component.InitPose(Pose);

		TMap<FName, FVector> Out;

		for (int32 Index = 0; Index < Reference.GetNum(); ++Index)
		{
			const FCompactPoseBoneIndex Compact = Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(Index));

			if (Compact.IsValid())
			{
				Out.Add(Reference.GetBoneName(Index), Component.GetComponentSpaceTransform(Compact).GetLocation());
			}
		}

		return Out;
	}

	FSpaceMMODeathClip Clip(const ESpaceMMOFall Fall, const TCHAR* Path)
	{
		FSpaceMMODeathClip Made;
		Made.Fall = Fall;
		Made.Clip = FSoftObjectPath(Path);

		return Made;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMODeathClipChosenByFallTest,
	"SpaceMMO.Death.AClipIsChosenByWhichWayTheBodyFalls",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMODeathClipChosenByFallTest::RunTest(const FString& Parameters)
{
	using namespace SpaceMMODeathClipTests;

	const TArray<FSpaceMMODeathClip> Clips = {
		Clip(ESpaceMMOFall::Backward, TEXT("/Game/A.A")),
		Clip(ESpaceMMOFall::Forward, TEXT("/Game/B.B")),
		Clip(ESpaceMMOFall::Backward, TEXT("/Game/C.C")),
	};

	const auto Path = [&](const ESpaceMMOFall Fall, const uint8 Variant)
	{
		const FSpaceMMODeathClip* Chosen = ASpaceMMOCharacterPawn::ChooseDeathClip(Clips, Fall, Variant);

		return Chosen != nullptr ? Chosen->Clip.ToString() : FString(TEXT("<none>"));
	};

	TestEqual(TEXT("Forward has one"), Path(ESpaceMMOFall::Forward, 7), FString(TEXT("/Game/B.B")));

	// Backward has two, and the variant takes turns between them, never the forward one.
	TestEqual(TEXT("Backward, variant 0"), Path(ESpaceMMOFall::Backward, 0), FString(TEXT("/Game/A.A")));
	TestEqual(TEXT("Backward, variant 1"), Path(ESpaceMMOFall::Backward, 1), FString(TEXT("/Game/C.C")));
	TestEqual(TEXT("Backward, variant 255"), Path(ESpaceMMOFall::Backward, 255), FString(TEXT("/Game/C.C")));

	// A fall nothing is authored for, or a character found dead with no fall at all, still lies down.
	TestEqual(TEXT("Unknown fall: any clip"), Path(ESpaceMMOFall::None, 1), FString(TEXT("/Game/B.B")));

	TestNull(TEXT("No clips: nothing"), ASpaceMMOCharacterPawn::ChooseDeathClip({}, ESpaceMMOFall::Forward, 0));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOEveryFallHasAClipEveryRaceCanPlayTest,
	"SpaceMMO.Death.EveryFallHasAClipEveryRaceCanPlay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOEveryFallHasAClipEveryRaceCanPlayTest::RunTest(const FString& Parameters)
{
	// The config as shipped, and the assets it names: a path that does not load, or a clip left on the template's
	// skeleton, is a body that stays standing, and nothing else would say so.
	const ASpaceMMOCharacterPawn* const Pawn = GetDefault<ASpaceMMOCharacterPawn>();

	const ESpaceMMOFall Falls[] = {
		ESpaceMMOFall::Backward, ESpaceMMOFall::Forward, ESpaceMMOFall::ToLeft, ESpaceMMOFall::ToRight,
	};

	for (const ESpaceMMOFall Fall : Falls)
	{
		const FString Name = StaticEnum<ESpaceMMOFall>()->GetNameStringByValue(static_cast<int64>(Fall));

		TestTrue(FString::Printf(TEXT("%s has a clip of its own"), *Name),
			Pawn->GetDeathClips().ContainsByPredicate([Fall](const FSpaceMMODeathClip& C) { return C.Fall == Fall; }));
	}

	for (const FSpaceMMODeathClip& Clip : Pawn->GetDeathClips())
	{
		const UAnimSequence* const Sequence = Cast<UAnimSequence>(Clip.Clip.TryLoad());

		if (!TestNotNull(FString::Printf(TEXT("%s loads"), *Clip.Clip.ToString()), Sequence))
		{
			continue;
		}

		// Held on its last frame: a clip that loops would get up and fall again for as long as they are dead.
		TestTrue(FString::Printf(TEXT("%s ends"), *Sequence->GetName()), Sequence->GetPlayLength() > 0.5);

		for (const FSpaceMMOCharacterBody& Body : Pawn->GetRaceBodies())
		{
			const USkeletalMesh* const Mesh = Cast<USkeletalMesh>(Body.Mesh.TryLoad());

			if (Mesh != nullptr)
			{
				TestTrue(FString::Printf(TEXT("%s plays on race %d's body"), *Sequence->GetName(), Body.Race),
					Sequence->GetSkeleton() == Mesh->GetSkeleton());
			}
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOEveryDeathEndsLyingTest,
	"SpaceMMO.Death.EveryDeathEndsLyingOnTheGroundOnEveryRace",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOEveryDeathEndsLyingTest::RunTest(const FString& Parameters)
{
	using namespace SpaceMMODeathClipTests;

	// Measured on the finished clip as each race's body plays it, at the height the pawn draws that body: the check
	// Epic's mannequin deaths would have failed (18 October), their pelvis still 80-112 cm up at the last frame. Joints,
	// not skin: a body lying on the ground has its joints a little above it, and one buried has them below.
	const ASpaceMMOCharacterPawn* const Pawn = GetDefault<ASpaceMMOCharacterPawn>();

	for (const FSpaceMMODeathClip& Clip : Pawn->GetDeathClips())
	{
		const UAnimSequence* const Sequence = Cast<UAnimSequence>(Clip.Clip.TryLoad());

		if (Sequence == nullptr)
		{
			continue;
		}

		for (const FSpaceMMOCharacterBody& Body : Pawn->GetRaceBodies())
		{
			const USkeletalMesh* const Mesh = Cast<USkeletalMesh>(Body.Mesh.TryLoad());

			if (!TestNotNull(FString::Printf(TEXT("Race %d's body loads"), Body.Race), Mesh))
			{
				continue;
			}

			const double Scale = ASpaceMMOCharacterPawn::UniformScaleForHeight(
				Mesh->GetBounds().BoxExtent.Z * 2.0, Body.HeightCentimetres);

			const TMap<FName, FVector> End = JointsAt(Sequence, Mesh, Sequence->GetPlayLength());
			const FVector* const Root = End.Find(TEXT("root"));
			const FVector* const Pelvis = End.Find(TEXT("pelvis"));
			const FVector* const Head = End.Find(TEXT("head"));

			if (!TestTrue(TEXT("Root, pelvis and head"), Root != nullptr && Pelvis != nullptr && Head != nullptr))
			{
				continue;
			}

			double Lowest = TNumericLimits<double>::Max();

			for (const TPair<FName, FVector>& Joint : End)
			{
				if (Joint.Key != TEXT("root") && !Joint.Key.ToString().StartsWith(TEXT("ik_")))
				{
					Lowest = FMath::Min(Lowest, (Joint.Value.Z - Root->Z) * Scale);
				}
			}

			const FString Where = FString::Printf(TEXT("%s on race %d"), *Sequence->GetName(), Body.Race);
			const double PelvisCm = (Pelvis->Z - Root->Z) * Scale;
			const double HeadCm = (Head->Z - Root->Z) * Scale;

			TestTrue(FString::Printf(TEXT("%s lies down: pelvis %.1f cm"), *Where, PelvisCm), PelvisCm < 30.0);
			TestTrue(FString::Printf(TEXT("%s lies down: head %.1f cm"), *Where, HeadCm), HeadCm < 40.0);
			TestTrue(FString::Printf(TEXT("%s is not under the ground: lowest joint %.1f cm"), *Where, Lowest), Lowest > -1.0);
		}
	}

	return true;
}

#endif
