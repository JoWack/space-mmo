#include "SpaceMMOImportClipsCommandlet.h"

#include "AnimGraphNode_BlendSpaceGraphBase.h"
#include "AnimGraphNode_Root.h"
#include "AnimGraphNode_Slot.h"
#include "AnimGraphNode_SequencePlayer.h"
#include "AnimationBlueprintLibrary.h"
#include "AnimationRuntime.h"
#include "BonePose.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/AnimNode_SequencePlayer.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace.h"
#include "Animation/Skeleton.h"
#include "AnimationBlendSpaceSampleGraph.h"
#include "AssetImportTask.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "BlendSpaceGraph.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "Engine/SkeletalMesh.h"
#include "Factories/AnimBlueprintFactory.h"
#include "Factories/FbxAnimSequenceImportData.h"
#include "Factories/FbxImportUI.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "IAssetTools.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "SpaceMMOAuthoringLog.h"
#include "SpaceMMOCharacterPawn.h"
#include "SpaceMMOWalkModel.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace SpaceMMOImportClips
{
	/** One clip: the FBX it comes from, the asset it becomes, and the clip it takes over from. */
	struct FClip
	{
		const TCHAR* Fbx;         // relative to the project directory
		const TCHAR* Folder;      // content folder for the sequence
		const TCHAR* Name;        // the sequence's asset name
		const TCHAR* Skeleton;    // object path of the skeleton it plays on
		const TCHAR* Blueprint;   // animation blueprint to point at it, or nullptr to point nothing at it
		const TCHAR* Replaces;    // object path of the sequence it takes over from there
		int32 Frames;             // keys Blender exported, at KeysPerSecond
		double PelvisMinCm;       // the pelvis's height over the clip, read back from the FBX in Blender
		double PelvisMaxCm;
		const TCHAR* AuthoredOn;  // the body it was made on, its retarget source; see below
		double KeysPerSecond = 30.0;  // 30 for every clip but the sprints; see them below
	};

	const TCHAR* const Mannequin =
		TEXT("/Game/FreeAnimationLibrary/Demo/Characters/Mannequins/Meshes/SK_Mannequin.SK_Mannequin");
	const TCHAR* const HumanoidMale = TEXT("/Game/Characters/Humanoid/SK_HumanoidMale.SK_HumanoidMale");
	const TCHAR* const MartianMale = TEXT("/Game/Characters/Martian/SK_MartianMale.SK_MartianMale");
	const TCHAR* const SpaceElfMale = TEXT("/Game/Characters/SpaceElf/SK_SpaceElfMale.SK_SpaceElfMale");
	const TCHAR* const SpaceOrcMale = TEXT("/Game/Characters/SpaceOrc/SK_SpaceOrcMale.SK_SpaceOrcMale");

	/**
	 * The Humanoid man's jump start (tasks 175 and 176, 4 October). It goes straight into the take-off,
	 * because the walk model leaves the ground on the frame jump is pressed and the library's clip crouched
	 * first. It covers take-off to apex -- 0.43 s at a JumpSpeed of 420 and gravity of 981 -- which is when
	 * ABP_Human's JumpStart hands over to Falling. Its pelvis read 92 to 96 cm in Blender.
	 *
	 * A clip made on a body plays scaled twice unless that body is its retarget source: this one stretched
	 * the Humanoid man's hips until it was (task 177). AuthoredOn sets it on every import. It used to be left
	 * to tools/characters/ue_import_character.py's AUTHORED_ON table, which still lists every such clip, still
	 * sets the same source when it runs, and fails on any anim_<body>_* clip it does not list.
	 */
	const FClip Clips[] = {
		{TEXT("RawContent/Characters/HumanoidMale/HumanoidMale.fbx"), TEXT("/Game/Characters/Humanoid"),
			TEXT("anim_HumanoidMale_JumpStart"), Mannequin,
			TEXT("/Game/Characters/Human/ABP_Human.ABP_Human"),
			TEXT("/Game/FreeAnimationLibrary/Animations/Jump/anim_InPlace_Jump_L.anim_InPlace_Jump_L"),
			16, 90.0, 98.0, HumanoidMale},

		// The Martian, elf and orc men's jump starts (task 182): 176's clip rebuilt on each body, its lifts scaled
		// by leg length. Nothing plays them: Joe chose the Humanoid man's jump for every race (176), which each
		// race's blueprint inherits from ABP_Human. Pelvis ranges are each export's, read back in Blender.
		{TEXT("RawContent/Characters/MartianMale/MartianMale.fbx"), TEXT("/Game/Characters/Martian"),
			TEXT("anim_MartianMale_JumpStart"), Mannequin, nullptr, nullptr, 16, 120.0, 130.0, MartianMale},
		{TEXT("RawContent/Characters/SpaceElfMale/SpaceElfMale.fbx"), TEXT("/Game/Characters/SpaceElf"),
			TEXT("anim_SpaceElfMale_JumpStart"), Mannequin, nullptr, nullptr, 16, 113.0, 123.0, SpaceElfMale},
		{TEXT("RawContent/Characters/SpaceOrcMale/SpaceOrcMale.fbx"), TEXT("/Game/Characters/SpaceOrc"),
			TEXT("anim_SpaceOrcMale_JumpStart"), Mannequin, nullptr, nullptr, 16, 106.0, 116.0, SpaceOrcMale},

		// Each race's sprint (task 191), made on its own rig by tools/characters/rig_character.py: two strides,
		// in place, the last frame the first, a planted foot sliding back at the sprint speed. Each export is the
		// body and this clip alone, <body>_Sprint.fbx, read back by verify_export.py with --loop-speed. They are
		// played from the sprint sample Locomotion below gives ABP_Human, not repointed into a graph.
		//
		// Keyed at 60 a second, not 30. The engine interpolates joints between keys, not the foot's path, and a
		// planted ball travelling 36 cm between keys at 30 sank as much as 4.4 cm into the ground halfway between
		// them, measured in game by SpaceMMO.Character.EachRaceSprintsOnItsOwnFeet.
		{TEXT("RawContent/Characters/HumanoidMale/HumanoidMale_Sprint.fbx"), TEXT("/Game/Characters/Humanoid"),
			TEXT("anim_HumanoidMale_Sprint"), Mannequin, nullptr, nullptr, 57, 81.0, 90.0, HumanoidMale, 60.0},
		{TEXT("RawContent/Characters/MartianMale/MartianMale_Sprint.fbx"), TEXT("/Game/Characters/Martian"),
			TEXT("anim_MartianMale_Sprint"), Mannequin, nullptr, nullptr, 73, 106.0, 114.0, MartianMale, 60.0},
		{TEXT("RawContent/Characters/SpaceElfMale/SpaceElfMale_Sprint.fbx"), TEXT("/Game/Characters/SpaceElf"),
			TEXT("anim_SpaceElfMale_Sprint"), Mannequin, nullptr, nullptr, 65, 103.0, 112.0, SpaceElfMale, 60.0},
		{TEXT("RawContent/Characters/SpaceOrcMale/SpaceOrcMale_Sprint.fbx"), TEXT("/Game/Characters/SpaceOrc"),
			TEXT("anim_SpaceOrcMale_Sprint"), Mannequin, nullptr, nullptr, 65, 95.0, 107.0, SpaceOrcMale, 60.0},

		// The four deaths (task 201), made once on the Humanoid man by tools/characters/rig_character.py's fall() and
		// played on every race, as his jump is: standing to lying in 1.3 s, then held. The pawn plays them by config
		// (DeathClips), not from a graph. The root stays put like every clip here's: the body falls by its pelvis, from
		// 92 cm to 15-17 (read off the first import, 18 October).
		{TEXT("RawContent/Characters/HumanoidMale/HumanoidMale_Death_Backward.fbx"), TEXT("/Game/Characters/Humanoid"),
			TEXT("anim_HumanoidMale_Death_Backward"), Mannequin, nullptr, nullptr, 41, 13.0, 94.0, HumanoidMale},
		{TEXT("RawContent/Characters/HumanoidMale/HumanoidMale_Death_Forward.fbx"), TEXT("/Game/Characters/Humanoid"),
			TEXT("anim_HumanoidMale_Death_Forward"), Mannequin, nullptr, nullptr, 41, 13.0, 94.0, HumanoidMale},
		{TEXT("RawContent/Characters/HumanoidMale/HumanoidMale_Death_ToLeft.fbx"), TEXT("/Game/Characters/Humanoid"),
			TEXT("anim_HumanoidMale_Death_ToLeft"), Mannequin, nullptr, nullptr, 41, 15.0, 94.0, HumanoidMale},
		// The dodge when standing still (task 195): a hop back, made on the Humanoid man by rig_character.py and
		// played on every race from ABP_Human's slot. The other dodge, the roll, is the library's (AdoptRoll below).
		{TEXT("RawContent/Characters/HumanoidMale/HumanoidMale_Dodge_Back.fbx"), TEXT("/Game/Characters/Humanoid"),
			TEXT("anim_HumanoidMale_Dodge_Back"), Mannequin, nullptr, nullptr, 16, 76.0, 119.0, HumanoidMale},
		{TEXT("RawContent/Characters/HumanoidMale/HumanoidMale_Death_ToRight.fbx"), TEXT("/Game/Characters/Humanoid"),
			TEXT("anim_HumanoidMale_Death_ToRight"), Mannequin, nullptr, nullptr, 41, 15.0, 94.0, HumanoidMale},
	};

	/**
	 * What each race's body plays (task 191). ABP_Human's speed blend space is embedded in it -- a blend space
	 * graph node, each sample a little graph holding a sequence player: idle, the library's walk and jog. It
	 * gains a sample at the sprint speed whose player plays the Humanoid man's sprint, which also plays before
	 * a race is known, since that draws him. Every other race gets a child of ABP_Human whose only change is a
	 * parent asset override on that one player, for the sprint made on its own body; its walk, jog, jump and
	 * state machine stay ABP_Human's. DefaultGame.ini's RaceBodies name each blueprint.
	 *
	 * /Game/Characters/Human/BS_Human_Speed is not it: nothing references that asset (read off the asset
	 * registry, 11 October); the blend space ABP_Human plays is its own copy, embedded.
	 */
	struct FLocomotion
	{
		const TCHAR* Body;        // the body the sprint was made on
		const TCHAR* Sprint;      // object path of the sprint sequence
		const TCHAR* Blueprint;   // the blueprint that plays it: ABP_Human, or a child of it made here
	};

	const TCHAR* const HumanBlueprint = TEXT("/Game/Characters/Human/ABP_Human.ABP_Human");

	/** The sync group ABP_Human's speed blend space keeps its samples in; nothing else in the blueprint uses it. */
	const FName SpeedSyncGroup(TEXT("SpeedBlend"));

	/** A library clip the speed blend space plays, and how many strides it holds. */
	struct FLibraryGait
	{
		const TCHAR* Clip;
		int32 Strides;
	};

	/**
	 * The walk and the run every race plays, which carry footfall markers as the sprints do. The run is what each
	 * sprint syncs with; the walk, because the sync group that keeps run and sprint in step keeps the walk with the
	 * run too, and it holds one stride against the run's two: matched by length, the walk's left foot comes down
	 * while the run's right is planted.
	 */
	const FLibraryGait LibraryGaits[] = {
		{TEXT("/Game/FreeAnimationLibrary/Animations/Walk/anim_Walk_Fwd_Loop_L.anim_Walk_Fwd_Loop_L"), 1},
		{TEXT("/Game/FreeAnimationLibrary/Animations/Jog/anim_Jog_Loop_Fwd.anim_Jog_Loop_Fwd"), 2},
	};

	const FLocomotion Locomotion[] = {
		{HumanoidMale, TEXT("/Game/Characters/Humanoid/anim_HumanoidMale_Sprint.anim_HumanoidMale_Sprint"), HumanBlueprint},
		{MartianMale, TEXT("/Game/Characters/Martian/anim_MartianMale_Sprint.anim_MartianMale_Sprint"),
			TEXT("/Game/Characters/Martian/ABP_MartianMale.ABP_MartianMale")},
		{SpaceElfMale, TEXT("/Game/Characters/SpaceElf/anim_SpaceElfMale_Sprint.anim_SpaceElfMale_Sprint"),
			TEXT("/Game/Characters/SpaceElf/ABP_SpaceElfMale.ABP_SpaceElfMale")},
		{SpaceOrcMale, TEXT("/Game/Characters/SpaceOrc/anim_SpaceOrcMale_Sprint.anim_SpaceOrcMale_Sprint"),
			TEXT("/Game/Characters/SpaceOrc/ABP_SpaceOrcMale.ABP_SpaceOrcMale")},
	};

	/** When this run began, so a save can be checked against the file on disk. */
	FDateTime StartedAt;

	/**
	 * Saves the asset's package, and checks the file on disk was written by this run. Beside an open editor a
	 * save can fail to replace the file and still leave every read-back of this process's memory agreeing
	 * with it; that cost task 177 a false fix, caught only by the file's time.
	 */
	bool Save(UObject* Asset)
	{
		UPackage* Package = Asset->GetOutermost();
		Package->MarkPackageDirty();

		const FString Filename = FPackageName::LongPackageNameToFilename(
			Package->GetName(), FPackageName::GetAssetPackageExtension());

		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.Error = GWarn;

		if (!UPackage::SavePackage(Package, Asset, *Filename, SaveArgs))
		{
			UE_LOG(LogSpaceMMOAuthoring, Error, TEXT("Clips: could not save %s."), *Filename);

			return false;
		}

		const FDateTime Written = IFileManager::Get().GetTimeStamp(*Filename);

		if (Written < StartedAt - FTimespan::FromSeconds(2.0))
		{
			UE_LOG(LogSpaceMMOAuthoring, Error, TEXT("Clips: %s was not written by this run (it is from %s): is an editor holding it?"),
				*Filename, *Written.ToString());

			return false;
		}

		return true;
	}

	/** Imports the clip's animation alone, onto the shared skeleton, through the legacy FBX importer. */
	UAnimSequence* Import(const FClip& Clip, USkeleton* Skeleton, int32& Problems)
	{
		// Interchange is the default for FBX in this engine and does not read UFbxImportUI (task 168).
		if (IConsoleVariable* Interchange = IConsoleManager::Get().FindConsoleVariable(
				TEXT("Interchange.FeatureFlags.Import.FBX")))
		{
			Interchange->Set(false, ECVF_SetByCode);
		}

		UFbxImportUI* Options = NewObject<UFbxImportUI>();
		Options->bIsObjImport = false;
		Options->bAutomatedImportShouldDetectType = false;
		Options->MeshTypeToImport = FBXIT_Animation;
		Options->OriginalImportType = FBXIT_SkeletalMesh;
		Options->bImportAsSkeletal = true;
		Options->bImportMesh = false;
		Options->bImportAnimations = true;
		Options->bImportMaterials = false;
		Options->bImportTextures = false;
		Options->bCreatePhysicsAsset = false;
		Options->Skeleton = Skeleton;

		UFbxAnimSequenceImportData* Anim = Options->AnimSequenceImportData;
		Anim->AnimationLength = FBXALIT_ExportedTime;
		Anim->bImportBoneTracks = true;
		Anim->bUseDefaultSampleRate = false;
		Anim->CustomSampleRate = static_cast<int32>(Clip.KeysPerSecond);
		Anim->bSnapToClosestFrameBoundary = true;
		Anim->bRemoveRedundantKeys = false;
		Anim->bImportCustomAttribute = false;
		Anim->bConvertScene = true;

		// Auto-Rig Pro writes centimetres, which this converts to nothing; a metre FBX it converts properly,
		// where leaving it off is the hundredth-size import that cost task 168 its first run. The pelvis's
		// height, checked below, is what proves which happened.
		Anim->bConvertSceneUnit = true;

		UAssetImportTask* Task = NewObject<UAssetImportTask>();
		Task->Filename = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / Clip.Fbx);
		Task->DestinationPath = Clip.Folder;
		Task->DestinationName = Clip.Name;
		Task->bAutomated = true;
		Task->bReplaceExisting = true;
		Task->bSave = false;
		Task->Options = Options;

		FAssetToolsModule::GetModule().Get().ImportAssetTasks({Task});

		UAnimSequence* Sequence = nullptr;

		for (UObject* Object : Task->GetObjects())
		{
			if (UAnimSequence* Imported = Cast<UAnimSequence>(Object))
			{
				Sequence = Imported;

				break;
			}
		}

		if (Sequence == nullptr)
		{
			UE_LOG(LogSpaceMMOAuthoring, Error, TEXT("Clips: importing %s produced no animation."), *Task->Filename);

			++Problems;

			return nullptr;
		}

		// The FBX importer names a take after its file when it cannot use the name it was given; the
		// blueprint and the task records refer to the name in the table, so that is what it must be.
		if (Sequence->GetName() != Clip.Name)
		{
			UE_LOG(LogSpaceMMOAuthoring, Display, TEXT("Clips: the import was named %s; renaming it %s."),
				*Sequence->GetName(), Clip.Name);

			TArray<FAssetRenameData> Renames;
			Renames.Emplace(Sequence, FString(Clip.Folder), FString(Clip.Name));

			if (!FAssetToolsModule::GetModule().Get().RenameAssets(Renames) || Sequence->GetName() != Clip.Name)
			{
				UE_LOG(LogSpaceMMOAuthoring, Error, TEXT("Clips: could not rename %s."), *Sequence->GetName());

				++Problems;
			}
		}

		return Sequence;
	}

	/** What was imported, against what Blender exported. Returns the number of disagreements. */
	int32 Check(const FClip& Clip, const UAnimSequence* Sequence, const USkeleton* Skeleton)
	{
		int32 Problems = 0;
		const IAnimationDataModel* Model = Sequence->GetDataModel();

		const int32 Keys = Model->GetNumberOfKeys();
		const double Rate = Model->GetFrameRate().AsDecimal();
		const double Length = Model->GetPlayLength();

		UE_LOG(LogSpaceMMOAuthoring, Display, TEXT("Clips: %s has %d keys at %.2f a second, %.3f s, %d bone tracks."),
			*Sequence->GetName(), Keys, Rate, Length, Model->GetNumBoneTracks());

		if (Sequence->GetSkeleton() != Skeleton)
		{
			UE_LOG(LogSpaceMMOAuthoring, Error, TEXT("Clips: %s is not on %s."), *Sequence->GetName(), Clip.Skeleton);

			++Problems;
		}

		if (Keys != Clip.Frames || !FMath::IsNearlyEqual(Rate, Clip.KeysPerSecond, 0.01))
		{
			UE_LOG(LogSpaceMMOAuthoring, Error, TEXT("Clips: expected %d keys at %.0f a second."), Clip.Frames, Clip.KeysPerSecond);

			++Problems;
		}

		if (!Sequence->bForceRootLock || Sequence->bEnableRootMotion)
		{
			UE_LOG(LogSpaceMMOAuthoring, Error, TEXT("Clips: %s must have Force Root Lock and no root motion."),
				*Sequence->GetName());

			++Problems;
		}

		const FName Root(TEXT("root"));
		const FName Pelvis(TEXT("pelvis"));

		if (!Model->IsValidBoneTrackName(Root) || !Model->IsValidBoneTrackName(Pelvis))
		{
			UE_LOG(LogSpaceMMOAuthoring, Error, TEXT("Clips: %s has no root or no pelvis track."), *Sequence->GetName());

			return Problems + 1;
		}

		// The root must not move: the server owns where a character is. The pelvis is measured in the
		// clip's own space, which is the root's, and its height is what shows the scale came through.
		const FVector RootStart = Model->GetBoneTrackTransform(Root, FFrameNumber(0)).GetLocation();
		double RootDrift = 0.0;
		double PelvisLow = TNumericLimits<double>::Max();
		double PelvisHigh = TNumericLimits<double>::Lowest();

		for (int32 Frame = 0; Frame < Keys; ++Frame)
		{
			const FTransform RootAt = Model->GetBoneTrackTransform(Root, FFrameNumber(Frame));
			const FTransform PelvisAt = Model->GetBoneTrackTransform(Pelvis, FFrameNumber(Frame)) * RootAt;

			RootDrift = FMath::Max(RootDrift, (RootAt.GetLocation() - RootStart).Size());
			PelvisLow = FMath::Min(PelvisLow, PelvisAt.GetLocation().Z);
			PelvisHigh = FMath::Max(PelvisHigh, PelvisAt.GetLocation().Z);
		}

		UE_LOG(LogSpaceMMOAuthoring, Display, TEXT("Clips: root moves %.3f cm; pelvis %.1f to %.1f cm (Blender read %.0f to %.0f)."),
			RootDrift, PelvisLow, PelvisHigh, Clip.PelvisMinCm, Clip.PelvisMaxCm);

		if (RootDrift > 0.01)
		{
			UE_LOG(LogSpaceMMOAuthoring, Error, TEXT("Clips: the root moves."));

			++Problems;
		}

		if (PelvisLow < Clip.PelvisMinCm || PelvisHigh > Clip.PelvisMaxCm)
		{
			UE_LOG(LogSpaceMMOAuthoring, Error,
				TEXT("Clips: the pelvis is outside %.0f-%.0f cm: the clip came in at the wrong scale or axes."),
				Clip.PelvisMinCm, Clip.PelvisMaxCm);

			++Problems;
		}

		// Read back, not assumed: a source that did not stick plays the clip scaled twice on its own body.
		const FString Source = Sequence->GetRetargetSourceAsset().ToSoftObjectPath().ToString();

		UE_LOG(LogSpaceMMOAuthoring, Display, TEXT("Clips: retarget source %s."), *Source);

		if (Source != Clip.AuthoredOn)
		{
			UE_LOG(LogSpaceMMOAuthoring, Error, TEXT("Clips: %s should retarget from %s, the body it was made on."),
				*Sequence->GetName(), Clip.AuthoredOn);

			++Problems;
		}

		return Problems;
	}

	/** Counts the sequence players in the blueprint that play Asset, and repoints those playing From to To. */
	int32 Players(UAnimBlueprint* Blueprint, const UAnimationAsset* Asset, const UAnimSequenceBase* From = nullptr,
		UAnimSequenceBase* To = nullptr)
	{
		TArray<UEdGraph*> Graphs;
		Blueprint->GetAllGraphs(Graphs);

		int32 Count = 0;

		for (UEdGraph* Graph : Graphs)
		{
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				UAnimGraphNode_SequencePlayer* Player = Cast<UAnimGraphNode_SequencePlayer>(Node);

				if (Player == nullptr)
				{
					continue;
				}

				if (From != nullptr && Player->GetAnimationAsset() == From)
				{
					Player->Modify();
					Player->SetAnimationAsset(To);

					// An exposed Sequence pin's default would override the node's own value.
					if (UEdGraphPin* Pin = Player->FindPin(TEXT("Sequence")); Pin != nullptr && Pin->DefaultObject == From)
					{
						Pin->DefaultObject = To;
					}
				}

				Count += Player->GetAnimationAsset() == Asset ? 1 : 0;
			}
		}

		return Count;
	}

	/** Points the blueprint's players of the replaced clip at the new one, compiles, and saves. */
	int32 Repoint(const FClip& Clip, UAnimSequence* Sequence)
	{
		UAnimBlueprint* Blueprint = LoadObject<UAnimBlueprint>(nullptr, Clip.Blueprint);
		UAnimSequenceBase* Old = LoadObject<UAnimSequenceBase>(nullptr, Clip.Replaces);

		if (Blueprint == nullptr || Old == nullptr)
		{
			UE_LOG(LogSpaceMMOAuthoring, Error, TEXT("Clips: could not load %s or %s."), Clip.Blueprint, Clip.Replaces);

			return 1;
		}

		const int32 Before = Players(Blueprint, Old);

		Players(Blueprint, Sequence, Old, Sequence);
		FKismetEditorUtilities::CompileBlueprint(Blueprint);

		if (Blueprint->Status == BS_Error)
		{
			UE_LOG(LogSpaceMMOAuthoring, Error, TEXT("Clips: %s did not compile."), Clip.Blueprint);

			return 1;
		}

		int32 Problems = Save(Blueprint) ? 0 : 1;

		// Counted again after the compile, off the graphs the editor will open.
		const int32 StillOld = Players(Blueprint, Old);
		const int32 Playing = Players(Blueprint, Sequence);

		UE_LOG(LogSpaceMMOAuthoring, Display, TEXT("Clips: %s played %s in %d place(s); now %d play %s and %d the old."),
			*Blueprint->GetName(), *Old->GetName(), Before, Playing, *Sequence->GetName(), StillOld);

		if (Playing < 1 || StillOld > 0)
		{
			UE_LOG(LogSpaceMMOAuthoring, Error, TEXT("Clips: %s does not play %s where it played %s."),
				*Blueprint->GetName(), *Sequence->GetName(), *Old->GetName());

			++Problems;
		}

		return Problems;
	}

	/**
	 * Footfall sync markers (task 191, Joe's 13 October playtest). The run is the library's jog, which keeps each foot
	 * down for a quarter of its stride; a sprint can keep it down for barely a seventh before the leg runs out of
	 * reach. Blended by fraction of their length, the run's planted, straight leg was averaged with the sprint's
	 * leg already folding up, and the foot swung across the body. With these on both, a sync group keeps them in
	 * step by footfall instead: foot down with foot down, foot up with foot up. The walk carries them too, for
	 * the same group keeps it with the run (LibraryGaits).
	 */
	const FName FootfallTrack(TEXT("Footfalls"));

	/** Where each foot lands and lifts in Sequence, as fractions of it, read off its balls of the feet. */
	struct FFootfalls
	{
		TArray<TPair<double, FName>> Events;
		FString Problem;
	};

	FFootfalls ReadFootfalls(const UAnimSequence* Sequence, USkeleton* Skeleton, const USkeletalMesh* Body, const int32 Strides)
	{
		FFootfalls Out;
		const FReferenceSkeleton& Reference = Body != nullptr ? Body->GetRefSkeleton() : Skeleton->GetReferenceSkeleton();
		TArray<FBoneIndexType> Required;

		for (int32 Index = 0; Index < Reference.GetNum(); ++Index)
		{
			Required.Add(static_cast<FBoneIndexType>(Index));
		}

		// On the body it was made on where there is one, retargeted as the game plays it; the library's walk and run on
		// the skeleton.
		FBoneContainer Bones;
		Bones.InitializeTo(Required, UE::Anim::FCurveFilterSettings(UE::Anim::ECurveFilterMode::DisallowAll),
			Body != nullptr ? static_cast<UObject&>(*const_cast<USkeletalMesh*>(Body)) : static_cast<UObject&>(*Skeleton));
		Bones.SetUseRAWData(true);

		const FName Balls[] = { TEXT("ball_l"), TEXT("ball_r") };
		const FName Downs[] = { TEXT("LeftFootDown"), TEXT("RightFootDown") };
		const FName Ups[] = { TEXT("LeftFootUp"), TEXT("RightFootUp") };
		constexpr int32 Samples = 512;
		const double Length = Sequence->GetPlayLength();
		const FCompactPoseBoneIndex Root = Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(Reference.FindBoneIndex(TEXT("root"))));
		TArray<double> Heights[2];

		for (int32 Sample = 0; Sample < Samples; ++Sample)
		{
			// Compact poses allocate from the memory stack, which wants a mark in scope (as the engine's AnimPose has).
			FMemMark Mark(FMemStack::Get());
			FCompactPose Pose;
			Pose.SetBoneContainer(&Bones);
			Pose.ResetToRefPose();
			FBlendedCurve Curve;
			Curve.InitFrom(Bones);
			UE::Anim::FStackAttributeContainer Attributes;
			FAnimationPoseData Data(Pose, Curve, Attributes);

			Sequence->GetAnimationPose(Data, FAnimExtractContext(Length * Sample / Samples, false));

			FCSPose<FCompactPose> Component;
			Component.InitPose(Pose);

			for (int32 Side = 0; Side < 2; ++Side)
			{
				const FCompactPoseBoneIndex Ball = Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(Reference.FindBoneIndex(Balls[Side])));
				Heights[Side].Add(Component.GetComponentSpaceTransform(Ball).GetLocation().Z
					- Component.GetComponentSpaceTransform(Root).GetLocation().Z);
			}
		}

		for (int32 Side = 0; Side < 2; ++Side)
		{
			// The ground under the ball: the height of its lowest twentieth, which a ball sagging between keys or a
			// foot rolling on its toes does not move far. Down is within 1.5 cm of it, for at least 6 samples running.
			TArray<double> Sorted = Heights[Side];
			Sorted.Sort();
			const double Ground = Sorted[Samples / 20];
			const auto IsDown = [&](const int32 Step) { return Heights[Side][Step % Samples] - Ground < 1.5; };
			int32 Landings = 0;

			// A landing is a sample down after one that is not, round the loop, so a stance across the end counts once.
			for (int32 Step = 0; Step < Samples; ++Step)
			{
				if (IsDown(Step) && !IsDown(Step + Samples - 1))
				{
					int32 End = Step;

					while (End < Step + Samples && IsDown(End))
					{
						++End;
					}

					if (End - Step >= 6)
					{
						Out.Events.Emplace(static_cast<double>(Step) / Samples, Downs[Side]);
						Out.Events.Emplace(static_cast<double>(End % Samples) / Samples, Ups[Side]);
						++Landings;
					}
				}
			}

			// A landing a foot a stride.
			if (Landings != Strides)
			{
				Out.Problem += FString::Printf(TEXT(" %s lands %d times, not %d;"), *Balls[Side].ToString(), Landings, Strides);
			}
		}

		Out.Events.Sort([](const TPair<double, FName>& A, const TPair<double, FName>& B) { return A.Key < B.Key; });

		return Out;
	}

	/** Replaces Sequence's footfall markers with those read off it, and reports them. Each sprint holds two strides. */
	int32 MarkFootfalls(UAnimSequence* Sequence, USkeleton* Skeleton, const USkeletalMesh* Body = nullptr, const int32 Strides = 2)
	{
		const FFootfalls Footfalls = ReadFootfalls(Sequence, Skeleton, Body, Strides);
		FString Listed;

		for (const TPair<double, FName>& Event : Footfalls.Events)
		{
			Listed += FString::Printf(TEXT(" %s %.3f"), *Event.Value.ToString(), Event.Key);
		}

		UE_LOG(LogSpaceMMOAuthoring, Display, TEXT("Clips: %s's footfalls, as fractions of it:%s."), *Sequence->GetName(), *Listed);

		if (!Footfalls.Problem.IsEmpty())
		{
			UE_LOG(LogSpaceMMOAuthoring, Error, TEXT("Clips: %s's footfalls are not %d strides':%s"), *Sequence->GetName(), Strides,
				*Footfalls.Problem);

			return 1;
		}

		Sequence->Modify();

		if (UAnimationBlueprintLibrary::IsValidAnimNotifyTrackName(Sequence, FootfallTrack))
		{
			UAnimationBlueprintLibrary::RemoveAnimationSyncMarkersByTrack(Sequence, FootfallTrack);
		}
		else
		{
			UAnimationBlueprintLibrary::AddAnimationNotifyTrack(Sequence, FootfallTrack);
		}

		for (const TPair<double, FName>& Event : Footfalls.Events)
		{
			UAnimationBlueprintLibrary::AddAnimationSyncMarker(Sequence, Event.Value,
				static_cast<float>(Event.Key * Sequence->GetPlayLength()), FootfallTrack);
		}

		// Read back off the sequence, not from the list just made.
		TArray<FAnimSyncMarker> Markers;
		UAnimationBlueprintLibrary::GetAnimationSyncMarkersForTrack(Sequence, FootfallTrack, Markers);

		if (Markers.Num() != Footfalls.Events.Num() || Markers.Num() != 4 * Strides)
		{
			UE_LOG(LogSpaceMMOAuthoring, Error, TEXT("Clips: %s carries %d footfall markers, not %d."), *Sequence->GetName(), Markers.Num(),
				4 * Strides);

			return 1;
		}

		return 0;
	}

	/** The sample at Speed, or INDEX_NONE. */
	int32 SampleAt(const UBlendSpace* Space, const double Speed)
	{
		for (int32 Index = 0; Index < Space->GetNumberOfBlendSamples(); ++Index)
		{
			if (FMath::IsNearlyEqual(Space->GetBlendSample(Index).SampleValue.X, Speed, 0.01))
			{
				return Index;
			}
		}

		return INDEX_NONE;
	}

	/** The sequence player in an embedded blend space's sample graph, or null. */
	UAnimGraphNode_SequencePlayer* SamplePlayer(const UEdGraph* Graph)
	{
		if (Graph == nullptr)
		{
			return nullptr;
		}

		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (UAnimGraphNode_SequencePlayer* Player = Cast<UAnimGraphNode_SequencePlayer>(Node))
			{
				return Player;
			}
		}

		return nullptr;
	}

	/** The blend space graph nodes in Blueprint: blend spaces embedded in it, rather than assets it plays. */
	TArray<UAnimGraphNode_BlendSpaceGraphBase*> EmbeddedBlendSpaces(UAnimBlueprint* Blueprint)
	{
		TArray<UEdGraph*> Graphs;
		Blueprint->GetAllGraphs(Graphs);

		TArray<UAnimGraphNode_BlendSpaceGraphBase*> Found;

		for (const UEdGraph* Graph : Graphs)
		{
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (UAnimGraphNode_BlendSpaceGraphBase* Embedded = Cast<UAnimGraphNode_BlendSpaceGraphBase>(Node))
				{
					Found.AddUnique(Embedded);
				}
			}
		}

		return Found;
	}

	/** An embedded blend space's samples, as "speed clip" pairs, for the log. */
	FString Describe(const UAnimGraphNode_BlendSpaceGraphBase* Node)
	{
		const UBlendSpace* const Space = Node->GetBlendSpaceGraph()->BlendSpace;
		FString Out;

		for (int32 Index = 0; Index < Space->GetNumberOfBlendSamples(); ++Index)
		{
			const UAnimGraphNode_SequencePlayer* const Player =
				Node->GetGraphs().IsValidIndex(Index) ? SamplePlayer(Node->GetGraphs()[Index]) : nullptr;
			const UAnimationAsset* const Plays = Player != nullptr ? Player->GetAnimationAsset() : nullptr;

			Out += FString::Printf(TEXT("%s%.2f %s"), Index == 0 ? TEXT("") : TEXT(", "),
				Space->GetBlendSample(Index).SampleValue.X, Plays != nullptr ? *Plays->GetName() : TEXT("<no player>"));
		}

		return Out;
	}

	/**
	 * Gives ABP_Human's embedded speed blend space a sample at the sprint speed playing the Humanoid man's sprint,
	 * compiles and saves it, and returns the sprint sample's sequence player, which each race's child overrides.
	 *
	 * The editor's own path: the sample added to the internal blend space, then a sample graph for it at the same
	 * index (AddGraph). A re-run finds the sample faster than a run and moves it to the sprint speed and its player
	 * to the clip, rather than removing and re-adding a graph: the node's RemoveGraph and ReplaceGraph each check
	 * for an open blueprint editor, which a commandlet does not have.
	 */
	int32 PlaceHumanSprint(UAnimBlueprint* Human, UAnimSequence* Sprint, const double SprintSpeed, const double RunSpeed,
		FGuid& OutPlayer)
	{
		const TArray<UAnimGraphNode_BlendSpaceGraphBase*> Embedded = EmbeddedBlendSpaces(Human);

		if (Embedded.Num() != 1)
		{
			UE_LOG(LogSpaceMMOAuthoring, Error, TEXT("Clips: %s embeds %d blend spaces; this expects its one speed blend space."),
				*Human->GetName(), Embedded.Num());

			return 1;
		}

		UAnimGraphNode_BlendSpaceGraphBase* const Node = Embedded[0];
		UBlendSpace* const Space = Node->GetBlendSpaceGraph() != nullptr ? Node->GetBlendSpaceGraph()->BlendSpace.Get() : nullptr;

		if (Space == nullptr || Space->GetNumberOfBlendSamples() != Node->GetGraphs().Num())
		{
			UE_LOG(LogSpaceMMOAuthoring, Error, TEXT("Clips: %s's embedded blend space and its sample graphs are out of step."),
				*Human->GetName());

			return 1;
		}

		UE_LOG(LogSpaceMMOAuthoring, Display, TEXT("Clips: %s's speed blend space played %s."), *Human->GetName(), *Describe(Node));

		// How its samples are kept in step: the node syncs them only as a named group (AnimNode_BlendSpaceGraphBase),
		// and each sample's player says how it joins one.
		FString Methods;

		for (const UEdGraph* Graph : Node->GetGraphs())
		{
			const UAnimGraphNode_SequencePlayer* const Player = SamplePlayer(Graph);

			Methods += FString::Printf(TEXT(" %s:%s"), Player != nullptr && Player->GetAnimationAsset() != nullptr
					? *Player->GetAnimationAsset()->GetName() : TEXT("<none>"),
				Player != nullptr ? *UEnum::GetValueAsString(Player->Node.GetGroupMethod()) : TEXT("-"));
		}

		UE_LOG(LogSpaceMMOAuthoring, Display, TEXT("Clips: its sync group is '%s'; its sample players sync by%s."),
			*Node->GetSyncGroupName().ToString(), *Methods);

		Human->Modify();
		Node->Modify();
		Space->Modify();

		// The sample faster than a run, if a run before this one made it.
		int32 Index = INDEX_NONE;

		for (int32 Sample = 0; Sample < Space->GetNumberOfBlendSamples(); ++Sample)
		{
			if (Space->GetBlendSample(Sample).SampleValue.X > RunSpeed + 0.01)
			{
				if (Index != INDEX_NONE)
				{
					UE_LOG(LogSpaceMMOAuthoring, Error, TEXT("Clips: %s has more than one sample faster than a run."), *Human->GetName());

					return 1;
				}

				Index = Sample;
			}
		}

		if (Index == INDEX_NONE)
		{
			Index = Space->AddSample(FVector(SprintSpeed, 0.0, 0.0));
			const UEdGraph* const Graph = Index != INDEX_NONE ? Node->AddGraph(FName(Sprint->GetName()), Sprint) : nullptr;

			if (Graph == nullptr || Node->GetSampleIndex(Graph) != Index)
			{
				UE_LOG(LogSpaceMMOAuthoring, Error, TEXT("Clips: could not add the sprint at %.2f to %s's blend space."),
					SprintSpeed, *Human->GetName());

				return 1;
			}
		}
		else if (!FMath::IsNearlyEqual(Space->GetBlendSample(Index).SampleValue.X, SprintSpeed, 0.01)
			&& !Space->EditSampleValue(Index, FVector(SprintSpeed, 0.0, 0.0)))
		{
			UE_LOG(LogSpaceMMOAuthoring, Error, TEXT("Clips: could not move %s's sprint sample to %.2f."), *Human->GetName(), SprintSpeed);

			return 1;
		}

		UAnimGraphNode_SequencePlayer* const Player = SamplePlayer(Node->GetGraphs()[Index]);

		if (Player == nullptr)
		{
			UE_LOG(LogSpaceMMOAuthoring, Error, TEXT("Clips: %s's sprint sample has no sequence player."), *Human->GetName());

			return 1;
		}

		Player->Modify();
		Player->SetAnimationAsset(Sprint);
		OutPlayer = Player->NodeGuid;

		// One clock for the samples. Without a group the node keeps none (AnimNode_BlendSpaceGraphBase syncs only a
		// named one) and every sample's player ran on its own: a sprint could come in on the opposite foot to the run
		// it blended with. Found 13 October, reading this node; walk and run were unsynced too.
		if (Node->GetSyncGroupName() != SpeedSyncGroup)
		{
			Node->SetSyncGroupName(SpeedSyncGroup);
		}

		// What plays at run time is rebuilt from the samples, in the order the editor does it.
		Space->ResampleData();
		Space->ValidateSampleData();

		FKismetEditorUtilities::CompileBlueprint(Human);

		if (Human->Status == BS_Error)
		{
			UE_LOG(LogSpaceMMOAuthoring, Error, TEXT("Clips: %s did not compile."), *Human->GetName());

			return 1;
		}

		UE_LOG(LogSpaceMMOAuthoring, Display, TEXT("Clips: %s's speed blend space plays %s, in sync group '%s'."),
			*Human->GetName(), *Describe(Node), *Node->GetSyncGroupName().ToString());

		if (Node->GetSyncGroupName() != SpeedSyncGroup)
		{
			UE_LOG(LogSpaceMMOAuthoring, Error, TEXT("Clips: %s's speed blend space is not in its sync group."), *Human->GetName());

			return 1;
		}

		const int32 Sprinting = SampleAt(Space, SprintSpeed);

		if (Sprinting != Index || SamplePlayer(Node->GetGraphs()[Index])->GetAnimationAsset() != Sprint)
		{
			UE_LOG(LogSpaceMMOAuthoring, Error, TEXT("Clips: %s does not play %s at %.2f m/s."), *Human->GetName(), *Sprint->GetName(), SprintSpeed);

			return 1;
		}

		return Save(Human) ? 0 : 1;
	}

	/** The child of Parent at Path, made through the editor's own animation blueprint factory if it is not there. */
	UAnimBlueprint* LoadOrMakeChild(const TCHAR* Path, UAnimBlueprint* Parent, USkeletalMesh* Body)
	{
		const FString Package = FPackageName::ObjectPathToPackageName(FString(Path));

		if (FPackageName::DoesPackageExist(Package))
		{
			return LoadObject<UAnimBlueprint>(nullptr, Path);
		}

		UAnimBlueprintFactory* Factory = NewObject<UAnimBlueprintFactory>();
		Factory->BlueprintType = BPTYPE_Normal;
		Factory->ParentClass = Parent->GeneratedClass.Get();
		Factory->TargetSkeleton = Parent->TargetSkeleton;
		Factory->PreviewSkeletalMesh = Body;
		Factory->bTemplate = false;

		return Cast<UAnimBlueprint>(FAssetToolsModule::GetModule().Get().CreateAsset(
			FPackageName::ObjectPathToObjectName(FString(Path)), FPackageName::GetLongPackagePath(Package),
			UAnimBlueprint::StaticClass(), Factory));
	}

	/** Has Child play Sprint where its parent's player Player does, compiles, saves, and reads back what plays. */
	int32 Override(UAnimBlueprint* Child, const FGuid& Player, UAnimSequence* Sprint)
	{
		Child->Modify();

		FAnimParentNodeAssetOverride* Existing = Child->ParentAssetOverrides.FindByPredicate(
			[&Player](const FAnimParentNodeAssetOverride& Override) { return Override.ParentNodeGuid == Player; });

		FAnimParentNodeAssetOverride& Override =
			Existing != nullptr ? *Existing : Child->ParentAssetOverrides.Emplace_GetRef(Player, Sprint);

		Override.NewAsset = Sprint;
		Child->NotifyOverrideChange(Override);

		FKismetEditorUtilities::CompileBlueprint(Child);

		if (Child->Status == BS_Error)
		{
			UE_LOG(LogSpaceMMOAuthoring, Error, TEXT("Clips: %s did not compile."), *Child->GetName());

			return 1;
		}

		int32 Problems = Save(Child) ? 0 : 1;

		// Read back: the override, and the compiled class's own node, which is what plays.
		UAnimBlueprintGeneratedClass* const Class = Cast<UAnimBlueprintGeneratedClass>(Child->GeneratedClass);
		UAnimInstance* const Defaults = Class != nullptr ? Class->GetDefaultObject<UAnimInstance>() : nullptr;
		const FAnimParentNodeAssetOverride* const Found = Child->GetAssetOverrideForNode(Player);
		const FAnimNode_SequencePlayer* const Node = Defaults != nullptr
			? Class->GetPropertyInstance<FAnimNode_SequencePlayer>(Defaults, Player, EPropertySearchMode::Hierarchy)
			: nullptr;
		const UAnimSequenceBase* const Plays = Node != nullptr ? Node->GetSequence() : nullptr;

		UE_LOG(LogSpaceMMOAuthoring, Display, TEXT("Clips: %s overrides the sprint sample with %s, and its class plays %s there."),
			*Child->GetName(), Found != nullptr && Found->NewAsset != nullptr ? *Found->NewAsset->GetName() : TEXT("<nothing>"),
			Plays != nullptr ? *Plays->GetName() : TEXT("<nothing>"));

		if (Found == nullptr || Found->NewAsset != Sprint || Plays != Sprint)
		{
			UE_LOG(LogSpaceMMOAuthoring, Error, TEXT("Clips: %s does not play %s."), *Child->GetName(), *Sprint->GetName());

			++Problems;
		}

		return Problems;
	}

	/** The slot every race's blueprint plays a short clip over its locomotion from: the dodges (task 195). */
	const FName DodgeSlot(TEXT("DefaultSlot"));

	/**
	 * Puts a slot between ABP_Human's locomotion and its output, unless one is there. Every race's blueprint is a child
	 * of it, so each plays from the same slot. Compiled and saved with the rest of BuildLocomotion.
	 */
	int32 EnsureSlot(UAnimBlueprint* Human)
	{
		UEdGraph* AnimGraph = nullptr;

		for (UEdGraph* Graph : Human->FunctionGraphs)
		{
			if (Graph != nullptr && Graph->GetFName() == TEXT("AnimGraph"))
			{
				AnimGraph = Graph;
			}
		}

		UAnimGraphNode_Root* Root = nullptr;

		if (AnimGraph != nullptr)
		{
			for (UEdGraphNode* Node : AnimGraph->Nodes)
			{
				Root = Root != nullptr ? Root : Cast<UAnimGraphNode_Root>(Node);
			}
		}

		UEdGraphPin* const Into = Root != nullptr ? Root->FindPin(TEXT("Result")) : nullptr;

		if (Into == nullptr || Into->LinkedTo.Num() != 1)
		{
			UE_LOG(LogSpaceMMOAuthoring, Error, TEXT("Clips: %s has no AnimGraph output with one thing feeding it."),
				*Human->GetName());

			return 1;
		}

		if (const UAnimGraphNode_Slot* Already = Cast<UAnimGraphNode_Slot>(Into->LinkedTo[0]->GetOwningNode()))
		{
			UE_LOG(LogSpaceMMOAuthoring, Display, TEXT("Clips: %s already plays its output through slot %s."),
				*Human->GetName(), *Already->Node.SlotName.ToString());

			return Already->Node.SlotName == DodgeSlot ? 0 : 1;
		}

		// The skeleton has to know the slot's name, or nothing plays in it.
		if (USkeleton* const Skeleton = Human->TargetSkeleton; Skeleton != nullptr && !Skeleton->ContainsSlotName(DodgeSlot))
		{
			Skeleton->Modify();
			Skeleton->RegisterSlotNode(DodgeSlot);
			Save(Skeleton);
		}

		Human->Modify();
		AnimGraph->Modify();

		FGraphNodeCreator<UAnimGraphNode_Slot> Creator(*AnimGraph);
		UAnimGraphNode_Slot* const Slot = Creator.CreateNode();
		Slot->Node.SlotName = DodgeSlot;
		Slot->NodePosX = Root->NodePosX - 260;
		Slot->NodePosY = Root->NodePosY;
		Creator.Finalize();

		UEdGraphPin* const Moving = Into->LinkedTo[0];
		UEdGraphPin* const SlotIn = Slot->FindPin(TEXT("Source"));
		UEdGraphPin* const SlotOut = Slot->FindPin(TEXT("Pose"));

		if (SlotIn == nullptr || SlotOut == nullptr)
		{
			UE_LOG(LogSpaceMMOAuthoring, Error, TEXT("Clips: the slot node has no Source or Pose pin."));

			return 1;
		}

		Into->BreakAllPinLinks();
		Moving->MakeLinkTo(SlotIn);
		SlotOut->MakeLinkTo(Into);

		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Human);

		UE_LOG(LogSpaceMMOAuthoring, Display, TEXT("Clips: %s's locomotion now passes through slot %s on its way out."),
			*Human->GetName(), *DodgeSlot.ToString());

		return 0;
	}

	const TCHAR* const LibraryRoll = TEXT("/Game/FreeAnimationLibrary/Animations/LandingRoll/anim_LandRoll_R.anim_LandRoll_R");
	const TCHAR* const DodgeRollFolder = TEXT("/Game/Characters/Shared");
	const TCHAR* const DodgeRollName = TEXT("anim_Dodge_Roll");

	/**
	 * The moving dodge (task 195): the library's motion-captured landing roll, copied so it can be locked to its root
	 * -- the character travels by the walk model, as with every clip here -- without changing the library's own. Logs
	 * how far the root has gone at every frame, which is the walk model's roll profile: the character moves at the
	 * capture's own speed through the roll, so the body never slides on the ground.
	 */
	int32 AdoptRoll()
	{
		const FString CopyPath = FString::Printf(TEXT("%s/%s.%s"), DodgeRollFolder, DodgeRollName, DodgeRollName);
		UAnimSequence* Copy = LoadObject<UAnimSequence>(nullptr, *CopyPath, nullptr, LOAD_NoWarn | LOAD_Quiet);

		if (Copy == nullptr)
		{
			UAnimSequence* const Source = LoadObject<UAnimSequence>(nullptr, LibraryRoll);

			if (Source == nullptr)
			{
				UE_LOG(LogSpaceMMOAuthoring, Error, TEXT("Clips: could not load %s."), LibraryRoll);

				return 1;
			}

			Copy = Cast<UAnimSequence>(FAssetToolsModule::GetModule().Get().DuplicateAsset(DodgeRollName, DodgeRollFolder, Source));
		}

		if (Copy == nullptr)
		{
			UE_LOG(LogSpaceMMOAuthoring, Error, TEXT("Clips: could not copy %s to %s."), LibraryRoll, *CopyPath);

			return 1;
		}

		Copy->Modify();
		Copy->bEnableRootMotion = false;
		Copy->bForceRootLock = true;

		int32 Problems = Save(Copy) ? 0 : 1;

		const IAnimationDataModel* const Model = Copy->GetDataModel();
		const int32 Keys = Model->GetNumberOfKeys();
		const double Rate = Model->GetFrameRate().AsDecimal();
		const FName Root(TEXT("root"));
		const FVector Start = Model->GetBoneTrackTransform(Root, FFrameNumber(0)).GetLocation();

		FString Profile;

		for (int32 Frame = 0; Frame < Keys; ++Frame)
		{
			const FVector At = Model->GetBoneTrackTransform(Root, FFrameNumber(Frame)).GetLocation() - Start;
			Profile += FString::Printf(TEXT("%s%.1f"), Frame == 0 ? TEXT("") : TEXT(","), At.Y);
		}

		UE_LOG(LogSpaceMMOAuthoring, Display, TEXT("Clips: %s, %d keys at %.2f a second, %.3f s; locked to its root %d."),
			*Copy->GetPathName(), Keys, Rate, Copy->GetPlayLength(), Copy->bForceRootLock ? 1 : 0);
		UE_LOG(LogSpaceMMOAuthoring, Display, TEXT("Clips: the roll's root, forward of where it began, cm by frame: %s"), *Profile);

		return Problems;
	}

	/** Each race's sprint where its body's blueprint plays it, and each race's blueprint (task 191). */
	int32 BuildLocomotion()
	{
		int32 Problems = 0;

		// Metres per second, the blend space's units, at the speeds the walk model runs and sprints at.
		const FWalkConfig& Walk = GetDefault<ASpaceMMOCharacterPawn>()->GetWalkConfig();
		const double SprintSpeed = Walk.TopSpeed(true) / 100.0;
		const double RunSpeed = Walk.TopSpeed(false) / 100.0;

		UAnimBlueprint* const Human = LoadObject<UAnimBlueprint>(nullptr, HumanBlueprint);

		if (Human == nullptr)
		{
			UE_LOG(LogSpaceMMOAuthoring, Error, TEXT("Clips: could not load %s."), HumanBlueprint);

			return 1;
		}

		// Before the sprint is placed, which compiles and saves it, and each race's child after it.
		Problems += EnsureSlot(Human);

		UE_LOG(LogSpaceMMOAuthoring, Display, TEXT("Clips: sprint at %.2f m/s, run at %.2f."), SprintSpeed, RunSpeed);

		// The walk and the run carry the same footfall markers, so the sync group can match each to the next foot by
		// foot. They are the library's clips, changed by this: a re-import of the library would drop them, and this
		// puts them back.
		for (const FLibraryGait& Gait : LibraryGaits)
		{
			UAnimSequence* const Clip = LoadObject<UAnimSequence>(nullptr, Gait.Clip);

			if (Clip == nullptr)
			{
				UE_LOG(LogSpaceMMOAuthoring, Error, TEXT("Clips: could not load %s."), Gait.Clip);

				return 1;
			}

			Problems += MarkFootfalls(Clip, Clip->GetSkeleton(), nullptr, Gait.Strides);
			Problems += Save(Clip) ? 0 : 1;
		}

		FGuid SprintPlayer;

		for (const FLocomotion& Row : Locomotion)
		{
			UAnimSequence* const Sprint = LoadObject<UAnimSequence>(nullptr, Row.Sprint);
			USkeletalMesh* const Body = LoadObject<USkeletalMesh>(nullptr, Row.Body);

			if (Sprint == nullptr || Body == nullptr)
			{
				UE_LOG(LogSpaceMMOAuthoring, Error, TEXT("Clips: could not load %s or %s."), Row.Sprint, Row.Body);

				++Problems;

				continue;
			}

			// The Humanoid man's row is first, and makes the sample every other row overrides.
			if (FCString::Strcmp(Row.Blueprint, HumanBlueprint) == 0)
			{
				Problems += PlaceHumanSprint(Human, Sprint, SprintSpeed, RunSpeed, SprintPlayer);

				continue;
			}

			UAnimBlueprint* const Child = SprintPlayer.IsValid() ? LoadOrMakeChild(Row.Blueprint, Human, Body) : nullptr;

			if (Child == nullptr || Child->ParentClass.Get() != Human->GeneratedClass.Get())
			{
				UE_LOG(LogSpaceMMOAuthoring, Error, TEXT("Clips: %s is not a child of %s, or there is no sprint sample to override."),
					Row.Blueprint, *Human->GetName());

				++Problems;

				continue;
			}

			Problems += Override(Child, SprintPlayer, Sprint);
		}

		return Problems;
	}
}

USpaceMMOImportClipsCommandlet::USpaceMMOImportClipsCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

int32 USpaceMMOImportClipsCommandlet::Main(const FString& Params)
{
	using namespace SpaceMMOImportClips;

	StartedAt = FDateTime::UtcNow();
	int32 Problems = 0;

	for (const FClip& Clip : Clips)
	{
		USkeleton* Skeleton = LoadObject<USkeleton>(nullptr, Clip.Skeleton);

		if (Skeleton == nullptr)
		{
			UE_LOG(LogSpaceMMOAuthoring, Error, TEXT("Clips: no skeleton at %s."), Clip.Skeleton);

			++Problems;

			continue;
		}

		UE_LOG(LogSpaceMMOAuthoring, Display, TEXT("Clips: %s from %s"), Clip.Name, Clip.Fbx);

		UAnimSequence* Sequence = Import(Clip, Skeleton, Problems);

		if (Sequence == nullptr)
		{
			continue;
		}

		Sequence->Modify();
		Sequence->bEnableRootMotion = false;
		Sequence->bForceRootLock = true;

		// Its retarget source is the body it was made on, so it plays on that body as authored (177).
		if (USkeletalMesh* const Body = LoadObject<USkeletalMesh>(nullptr, Clip.AuthoredOn))
		{
			Sequence->SetRetargetSourceAsset(Body);
			Sequence->UpdateRetargetSourceAssetData();
		}
		else
		{
			UE_LOG(LogSpaceMMOAuthoring, Error, TEXT("Clips: no body at %s to retarget %s from."), Clip.AuthoredOn, Clip.Name);

			++Problems;
		}

		FAssetRegistryModule::AssetCreated(Sequence);

		// Every sprint is synced to the run by footfall (task 191); nothing else here loops on a stride.
		if (FString(Clip.Name).EndsWith(TEXT("_Sprint")))
		{
			Problems += MarkFootfalls(Sequence, Skeleton, LoadObject<USkeletalMesh>(nullptr, Clip.AuthoredOn));
		}

		Problems += Save(Sequence) ? 0 : 1;
		Problems += Check(Clip, Sequence, Skeleton);

		if (Clip.Blueprint != nullptr)
		{
			Problems += Repoint(Clip, Sequence);
		}
		else
		{
			UE_LOG(LogSpaceMMOAuthoring, Display, TEXT("Clips: %s is played from no graph node directly."), Clip.Name);
		}
	}

	Problems += AdoptRoll();
	Problems += BuildLocomotion();

	UE_LOG(LogSpaceMMOAuthoring, Display, TEXT("Clips: %d problem(s). Result: %s"),
		Problems, Problems == 0 ? TEXT("OK") : TEXT("FAILED"));

	return Problems == 0 ? 0 : 1;
}
