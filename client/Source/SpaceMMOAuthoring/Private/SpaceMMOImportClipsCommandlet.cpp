#include "SpaceMMOImportClipsCommandlet.h"

#include "AnimGraphNode_SequencePlayer.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "AssetImportTask.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "Factories/FbxAnimSequenceImportData.h"
#include "Factories/FbxImportUI.h"
#include "HAL/IConsoleManager.h"
#include "IAssetTools.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "SpaceMMOAuthoringLog.h"
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
		const TCHAR* Blueprint;   // animation blueprint to point at it
		const TCHAR* Replaces;    // object path of the sequence it takes over from there
		int32 Frames;             // frames Blender exported, at 30 a second
		double PelvisMinCm;       // the pelvis's height over the clip, read back from the FBX in Blender
		double PelvisMaxCm;
	};

	/**
	 * The Humanoid man's jump start (tasks 175 and 176, 4 October). It goes straight into the take-off,
	 * because the walk model leaves the ground on the frame jump is pressed and the library's clip crouched
	 * first. It covers take-off to apex -- 0.43 s at a JumpSpeed of 420 and gravity of 981 -- which is when
	 * ABP_Human's JumpStart hands over to Falling. Its pelvis read 92 to 96 cm in Blender.
	 *
	 * A clip made on a body plays scaled twice unless that body is its retarget source: this one stretched
	 * the Humanoid man's hips until it was (task 177). tools/characters/ue_import_character.py sets it, from
	 * its AUTHORED_ON table, and fails on any anim_<body>_* clip that table does not list; run it after this.
	 * A re-run of this keeps the source (tested 4 October).
	 */
	const FClip Clips[] = {
		{TEXT("RawContent/Characters/HumanoidMale/HumanoidMale.fbx"), TEXT("/Game/Characters/Humanoid"),
			TEXT("anim_HumanoidMale_JumpStart"),
			TEXT("/Game/FreeAnimationLibrary/Demo/Characters/Mannequins/Meshes/SK_Mannequin.SK_Mannequin"),
			TEXT("/Game/Characters/Human/ABP_Human.ABP_Human"),
			TEXT("/Game/FreeAnimationLibrary/Animations/Jump/anim_InPlace_Jump_L.anim_InPlace_Jump_L"),
			16, 90.0, 98.0},
	};

	constexpr double FramesPerSecond = 30.0;

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
		Anim->CustomSampleRate = static_cast<int32>(FramesPerSecond);
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

		if (Keys != Clip.Frames || !FMath::IsNearlyEqual(Rate, FramesPerSecond, 0.01))
		{
			UE_LOG(LogSpaceMMOAuthoring, Error, TEXT("Clips: expected %d keys at %.0f a second."), Clip.Frames, FramesPerSecond);

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
		FAssetRegistryModule::AssetCreated(Sequence);

		Problems += Save(Sequence) ? 0 : 1;
		Problems += Check(Clip, Sequence, Skeleton);
		Problems += Repoint(Clip, Sequence);
	}

	UE_LOG(LogSpaceMMOAuthoring, Display, TEXT("Clips: %d problem(s). Result: %s"),
		Problems, Problems == 0 ? TEXT("OK") : TEXT("FAILED"));

	return Problems == 0 ? 0 : 1;
}
