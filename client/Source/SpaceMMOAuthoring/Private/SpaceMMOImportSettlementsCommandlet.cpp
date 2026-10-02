#include "SpaceMMOImportSettlementsCommandlet.h"

#include "AssetImportTask.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/Blueprint.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/StaticMesh.h"
#include "Factories/FbxImportUI.h"
#include "Factories/FbxStaticMeshImportData.h"
#include "Factories/MaterialFactoryNew.h"
#include "Factories/MaterialInstanceConstantFactoryNew.h"
#include "HAL/IConsoleManager.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "PhysicsEngine/BodySetup.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "SpaceMMOAuthoringLog.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace SpaceMMOImportSettlements
{
	/** One settlement: where its generated files are and where its assets go. */
	struct FSettlement
	{
		const TCHAR* Name;
		const TCHAR* RawFolder;
		const TCHAR* Fbx;
		const TCHAR* Manifest;
		const TCHAR* ContentFolder;
		const TCHAR* Blueprint;
	};

	const FSettlement Settlements[] = {
		{TEXT("Borlash"), TEXT("RawContent/Stations/A07_BorlashCity"), TEXT("Borlash.fbx"),
			TEXT("Borlash_manifest.json"), TEXT("/Game/Stations/Borlash"), TEXT("BP_Station_Borlash")},
	};

	const TCHAR* const SettlementClassPath = TEXT("/Script/SpaceMMOBackend.SpaceMMOSettlementActor");
	const TCHAR* const BerthClassPath = TEXT("/Script/SpaceMMOBackend.SpaceMMOBerthComponent");
	const TCHAR* const ServicePointClassPath = TEXT("/Script/SpaceMMOBackend.SpaceMMOServicePointComponent");

	/**
	 * How strongly an emissive surface glows here, per unit of Blender's emission strength.
	 *
	 * Blender's preview is lit by a sky of strength 0.25 and Unreal's by an auto-exposed sun, so the
	 * same number does not glow the same. A first guess, to be tuned by eye; one constant so the tuning
	 * is one edit.
	 */
	constexpr float EmissiveScale = 4.0f;

	/** Blender metres (+X east, +Y north, +Z up) to Unreal centimetres: the FBX importer flips Y. */
	FVector ToUnreal(const FVector& Blender)
	{
		return FVector(Blender.X * 100.0, -Blender.Y * 100.0, Blender.Z * 100.0);
	}

	FVector ReadVector(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
	{
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;

		if (!Object->TryGetArrayField(Field, Values) || Values == nullptr || Values->Num() < 3)
		{
			return FVector::ZeroVector;
		}

		return FVector((*Values)[0]->AsNumber(), (*Values)[1]->AsNumber(), (*Values)[2]->AsNumber());
	}

	FLinearColor ReadColour(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
	{
		const FVector Colour = ReadVector(Object, Field);

		return FLinearColor(static_cast<float>(Colour.X), static_cast<float>(Colour.Y), static_cast<float>(Colour.Z), 1.0f);
	}

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
			UE_LOG(LogSpaceMMOAuthoring, Error, TEXT("Settlements: could not save %s."), *Filename);

			return false;
		}

		return true;
	}

	// ---- reflection, because this module links nothing that talks to the backend ------------------

	bool SetDouble(UObject* Object, const TCHAR* Name, const double Value)
	{
		FDoubleProperty* Property = FindFProperty<FDoubleProperty>(Object->GetClass(), Name);

		if (Property == nullptr)
		{
			UE_LOG(LogSpaceMMOAuthoring, Error, TEXT("Settlements: %s has no double %s."), *Object->GetClass()->GetName(), Name);

			return false;
		}

		Property->SetPropertyValue_InContainer(Object, Value);

		return true;
	}

	bool SetString(UObject* Object, const TCHAR* Name, const FString& Value)
	{
		FStrProperty* Property = FindFProperty<FStrProperty>(Object->GetClass(), Name);

		if (Property == nullptr)
		{
			UE_LOG(LogSpaceMMOAuthoring, Error, TEXT("Settlements: %s has no string %s."), *Object->GetClass()->GetName(), Name);

			return false;
		}

		Property->SetPropertyValue_InContainer(Object, Value);

		return true;
	}

	bool SetBool(UObject* Object, const TCHAR* Name, const bool bValue)
	{
		FBoolProperty* Property = FindFProperty<FBoolProperty>(Object->GetClass(), Name);

		if (Property == nullptr)
		{
			UE_LOG(LogSpaceMMOAuthoring, Error, TEXT("Settlements: %s has no bool %s."), *Object->GetClass()->GetName(), Name);

			return false;
		}

		Property->SetPropertyValue_InContainer(Object, bValue);

		return true;
	}

	bool SetVector(UObject* Object, const TCHAR* Name, const FVector& Value)
	{
		FStructProperty* Property = FindFProperty<FStructProperty>(Object->GetClass(), Name);

		if (Property == nullptr || Property->Struct != TBaseStructure<FVector>::Get())
		{
			UE_LOG(LogSpaceMMOAuthoring, Error, TEXT("Settlements: %s has no vector %s."), *Object->GetClass()->GetName(), Name);

			return false;
		}

		*Property->ContainerPtrToValuePtr<FVector>(Object) = Value;

		return true;
	}

	bool SetStrings(UObject* Object, const TCHAR* Name, const TArray<FString>& Values)
	{
		FArrayProperty* Property = FindFProperty<FArrayProperty>(Object->GetClass(), Name);

		if (Property == nullptr || !Property->Inner->IsA<FStrProperty>())
		{
			UE_LOG(LogSpaceMMOAuthoring, Error, TEXT("Settlements: %s has no string array %s."), *Object->GetClass()->GetName(), Name);

			return false;
		}

		*Property->ContainerPtrToValuePtr<TArray<FString>>(Object) = Values;

		return true;
	}

	// ---- the steps ----------------------------------------------------------------------------------

	/**
	 * The parent material every settlement surface shares: four parameters and an emissive product.
	 *
	 * Built once and kept. A colour, a metalness, a roughness and a glow are all the manifest says
	 * about a surface, and they live on the instances, so nothing about a city's look is in this graph.
	 */
	UMaterial* BaseMaterial(const FString& Folder, int32& Problems)
	{
		const FString Name = TEXT("M_Settlement_Base");
		const FString PackageName = Folder / Name;

		if (UMaterial* Existing = LoadObject<UMaterial>(nullptr, *(PackageName + TEXT(".") + Name)))
		{
			return Existing;
		}

		UPackage* Package = CreatePackage(*PackageName);
		Package->FullyLoad();

		UMaterialFactoryNew* Factory = NewObject<UMaterialFactoryNew>();
		UMaterial* Material = Cast<UMaterial>(Factory->FactoryCreateNew(
			UMaterial::StaticClass(), Package, FName(*Name), RF_Public | RF_Standalone | RF_Transactional,
			nullptr, GWarn));

		if (Material == nullptr)
		{
			++Problems;

			return nullptr;
		}

		auto* BaseColour = Cast<UMaterialExpressionVectorParameter>(UMaterialEditingLibrary::CreateMaterialExpression(
			Material, UMaterialExpressionVectorParameter::StaticClass(), -700, -200));
		BaseColour->ParameterName = TEXT("BaseColor");
		BaseColour->DefaultValue = FLinearColor(0.5f, 0.5f, 0.5f, 1.0f);

		auto* Metallic = Cast<UMaterialExpressionScalarParameter>(UMaterialEditingLibrary::CreateMaterialExpression(
			Material, UMaterialExpressionScalarParameter::StaticClass(), -700, 0));
		Metallic->ParameterName = TEXT("Metallic");
		Metallic->DefaultValue = 0.0f;

		auto* Roughness = Cast<UMaterialExpressionScalarParameter>(UMaterialEditingLibrary::CreateMaterialExpression(
			Material, UMaterialExpressionScalarParameter::StaticClass(), -700, 100));
		Roughness->ParameterName = TEXT("Roughness");
		Roughness->DefaultValue = 0.7f;

		auto* Emissive = Cast<UMaterialExpressionVectorParameter>(UMaterialEditingLibrary::CreateMaterialExpression(
			Material, UMaterialExpressionVectorParameter::StaticClass(), -900, 250));
		Emissive->ParameterName = TEXT("EmissiveColor");
		Emissive->DefaultValue = FLinearColor::Black;

		auto* Strength = Cast<UMaterialExpressionScalarParameter>(UMaterialEditingLibrary::CreateMaterialExpression(
			Material, UMaterialExpressionScalarParameter::StaticClass(), -900, 450));
		Strength->ParameterName = TEXT("EmissiveStrength");
		Strength->DefaultValue = 0.0f;

		auto* Glow = Cast<UMaterialExpressionMultiply>(UMaterialEditingLibrary::CreateMaterialExpression(
			Material, UMaterialExpressionMultiply::StaticClass(), -400, 300));

		const bool bWired =
			UMaterialEditingLibrary::ConnectMaterialProperty(BaseColour, TEXT(""), MP_BaseColor)
			&& UMaterialEditingLibrary::ConnectMaterialProperty(Metallic, TEXT(""), MP_Metallic)
			&& UMaterialEditingLibrary::ConnectMaterialProperty(Roughness, TEXT(""), MP_Roughness)
			&& UMaterialEditingLibrary::ConnectMaterialExpressions(Emissive, TEXT(""), Glow, TEXT("A"))
			&& UMaterialEditingLibrary::ConnectMaterialExpressions(Strength, TEXT(""), Glow, TEXT("B"))
			&& UMaterialEditingLibrary::ConnectMaterialProperty(Glow, TEXT(""), MP_EmissiveColor);

		if (!bWired)
		{
			UE_LOG(LogSpaceMMOAuthoring, Error, TEXT("Settlements: the base material's graph did not wire up."));

			++Problems;
		}

		UMaterialEditingLibrary::RecompileMaterial(Material);
		FAssetRegistryModule::AssetCreated(Material);

		if (!Save(Material))
		{
			++Problems;
		}

		return Material;
	}

	/** One instance per manifest material, created or updated, keyed by the slot name it fills. */
	TMap<FName, UMaterialInstanceConstant*> Instances(
		const TSharedPtr<FJsonObject>& Manifest, UMaterial* Base, const FString& Folder, int32& Problems)
	{
		TMap<FName, UMaterialInstanceConstant*> Made;
		const TSharedPtr<FJsonObject> Materials = Manifest->GetObjectField(TEXT("materials"));

		for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : Materials->Values)
		{
			const FString Slot = Entry.Key;
			const TSharedPtr<FJsonObject> Look = Entry.Value->AsObject();
			const FString Name = TEXT("MI_") + Slot.Replace(TEXT("MAT_"), TEXT(""));
			const FString PackageName = Folder / Name;

			UMaterialInstanceConstant* Instance =
				LoadObject<UMaterialInstanceConstant>(nullptr, *(PackageName + TEXT(".") + Name));

			if (Instance == nullptr)
			{
				UPackage* Package = CreatePackage(*PackageName);
				Package->FullyLoad();

				UMaterialInstanceConstantFactoryNew* Factory = NewObject<UMaterialInstanceConstantFactoryNew>();
				Factory->InitialParent = Base;

				Instance = Cast<UMaterialInstanceConstant>(Factory->FactoryCreateNew(
					UMaterialInstanceConstant::StaticClass(), Package, FName(*Name),
					RF_Public | RF_Standalone | RF_Transactional, nullptr, GWarn));

				if (Instance == nullptr)
				{
					++Problems;

					continue;
				}

				FAssetRegistryModule::AssetCreated(Instance);
			}

			UMaterialEditingLibrary::SetMaterialInstanceParent(Instance, Base);
			UMaterialEditingLibrary::SetMaterialInstanceVectorParameterValue(
				Instance, TEXT("BaseColor"), ReadColour(Look, TEXT("base_colour_linear")));
			UMaterialEditingLibrary::SetMaterialInstanceScalarParameterValue(
				Instance, TEXT("Metallic"), static_cast<float>(Look->GetNumberField(TEXT("metallic"))));
			UMaterialEditingLibrary::SetMaterialInstanceScalarParameterValue(
				Instance, TEXT("Roughness"), static_cast<float>(Look->GetNumberField(TEXT("roughness"))));

			const TArray<TSharedPtr<FJsonValue>>* Emission = nullptr;
			const bool bGlows = Look->TryGetArrayField(TEXT("emission_linear"), Emission) && Emission != nullptr;

			UMaterialEditingLibrary::SetMaterialInstanceVectorParameterValue(
				Instance, TEXT("EmissiveColor"),
				bGlows ? ReadColour(Look, TEXT("emission_linear")) : FLinearColor::Black);
			UMaterialEditingLibrary::SetMaterialInstanceScalarParameterValue(
				Instance, TEXT("EmissiveStrength"),
				bGlows ? static_cast<float>(Look->GetNumberField(TEXT("emission_strength"))) * EmissiveScale : 0.0f);

			UMaterialEditingLibrary::UpdateMaterialInstance(Instance);

			if (!Save(Instance))
			{
				++Problems;
			}

			Made.Add(FName(*Slot), Instance);
		}

		return Made;
	}

	/** Imports the FBX through the legacy importer, with the options the greybox method needs. */
	TArray<UStaticMesh*> ImportMeshes(const FString& Fbx, const FString& Folder, int32& Problems)
	{
		// Interchange is the default for FBX in this engine and does not read UFbxImportUI. The legacy
		// importer does, and is what bOneConvexHullPerUCX belongs to: without it a UCX hull may be
		// decomposed again, though every one the script writes is already a single convex solid.
		if (IConsoleVariable* Interchange = IConsoleManager::Get().FindConsoleVariable(
				TEXT("Interchange.FeatureFlags.Import.FBX")))
		{
			Interchange->Set(false, ECVF_SetByCode);
		}

		UFbxImportUI* Options = NewObject<UFbxImportUI>();
		Options->bIsObjImport = false;
		Options->bAutomatedImportShouldDetectType = false;
		Options->MeshTypeToImport = FBXIT_StaticMesh;
		Options->bImportAsSkeletal = false;
		Options->bImportMesh = true;
		Options->bImportMaterials = false;
		Options->bImportTextures = false;
		Options->bImportAnimations = false;

		UFbxStaticMeshImportData* Mesh = Options->StaticMeshImportData;
		Mesh->bCombineMeshes = false;
		Mesh->bAutoGenerateCollision = false;
		Mesh->bOneConvexHullPerUCX = true;
		Mesh->bGenerateLightmapUVs = false;
		Mesh->bBuildNanite = false;
		Mesh->bRemoveDegenerates = false;
		Mesh->NormalImportMethod = FBXNIM_ImportNormals;
		Mesh->bTransformVertexToAbsolute = true;
		Mesh->bBakePivotInVertex = false;
		Mesh->ImportUniformScale = 1.0f;
		Mesh->bConvertScene = true;
		Mesh->bForceFrontXAxis = false;

		// The FBX says it is in metres (UnitScaleFactor 100, from Blender's FBX_SCALE_UNITS) and holds
		// metres. This importer ignores that unless told, and reads every metre as a centimetre: the
		// first run brought Borlash in at a hundredth of its size, hulls all present and every bounds
		// check off by 99% (2 October). FbxMainImport.cpp converts only when this is set.
		Mesh->bConvertSceneUnit = true;

		UAssetImportTask* Task = NewObject<UAssetImportTask>();
		Task->Filename = Fbx;
		Task->DestinationPath = Folder;
		Task->bAutomated = true;
		Task->bReplaceExisting = true;
		Task->bSave = false;
		Task->Options = Options;

		FAssetToolsModule::GetModule().Get().ImportAssetTasks({Task});

		TArray<UStaticMesh*> Meshes;

		for (UObject* Object : Task->GetObjects())
		{
			if (UStaticMesh* Imported = Cast<UStaticMesh>(Object))
			{
				Meshes.Add(Imported);
			}
		}

		if (Meshes.Num() == 0)
		{
			UE_LOG(LogSpaceMMOAuthoring, Error, TEXT("Settlements: importing %s produced no meshes."), *Fbx);

			++Problems;
		}

		return Meshes;
	}

	/**
	 * The manifest's name for an imported mesh. The legacy importer names each mesh of a multi-mesh file
	 * "<file>_<mesh>" -- Borlash_SM_Borlash_Admin -- which is what the first run of this measured.
	 */
	FString ManifestName(const UStaticMesh* Mesh, const FString& FbxBase)
	{
		const FString Prefix = FbxBase + TEXT("_");

		return Mesh->GetName().StartsWith(Prefix) ? Mesh->GetName().RightChop(Prefix.Len()) : Mesh->GetName();
	}

	/** Each mesh against its manifest row: hulls counted, bounds compared after the axis flip. */
	int32 CheckMeshes(
		const TArray<UStaticMesh*>& Meshes, const TSharedPtr<FJsonObject>& Manifest, const FString& FbxBase)
	{
		int32 Problems = 0;
		TMap<FString, UStaticMesh*> ByName;

		for (UStaticMesh* Mesh : Meshes)
		{
			ByName.Add(ManifestName(Mesh, FbxBase), Mesh);
		}

		for (const TSharedPtr<FJsonValue>& Value : Manifest->GetArrayField(TEXT("meshes")))
		{
			const TSharedPtr<FJsonObject> Row = Value->AsObject();
			const FString Name = Row->GetStringField(TEXT("name"));
			UStaticMesh* const* Found = ByName.Find(Name);

			if (Found == nullptr)
			{
				UE_LOG(LogSpaceMMOAuthoring, Error, TEXT("Settlements: %s is in the manifest and was not imported."), *Name);

				++Problems;

				continue;
			}

			const int32 Hulls = (*Found)->GetBodySetup() != nullptr
				? (*Found)->GetBodySetup()->AggGeom.ConvexElems.Num()
				: 0;
			const int32 Expected = static_cast<int32>(Row->GetNumberField(TEXT("hulls")));

			// Blender's [min, max] per axis, then into Unreal's axes: Y flips, so its min and max swap.
			const TArray<TSharedPtr<FJsonValue>>& B = Row->GetArrayField(TEXT("bounds_m"));
			const FVector Lo(B[0]->AsArray()[0]->AsNumber(), B[1]->AsArray()[0]->AsNumber(), B[2]->AsArray()[0]->AsNumber());
			const FVector Hi(B[0]->AsArray()[1]->AsNumber(), B[1]->AsArray()[1]->AsNumber(), B[2]->AsArray()[1]->AsNumber());
			const FBox Want(
				FVector(Lo.X * 100.0, -Hi.Y * 100.0, Lo.Z * 100.0),
				FVector(Hi.X * 100.0, -Lo.Y * 100.0, Hi.Z * 100.0));
			const FBox Got = (*Found)->GetBoundingBox();
			const double Error = FMath::Max(
				(Got.Min - Want.Min).GetAbsMax(), (Got.Max - Want.Max).GetAbsMax());

			// The hulls, separately. They are imported by a different path from the triangles, and the
			// render bounds say nothing about them: a city drawn at full size over collision a hundredth
			// of it would pass every line above and be walked through in play. Inside the drawn mesh,
			// and spanning most of it across, is what a hull set at the right scale looks like.
			bool bHullsFit = true;
			FBox HullBox(ForceInit);

			if (Hulls > 0)
			{
				HullBox = (*Found)->GetBodySetup()->AggGeom.CalcAABB(FTransform::Identity);

				const bool bInside = Got.ExpandBy(5.0).IsInside(HullBox);
				const double Spans = FMath::Min(
					HullBox.GetSize().X / FMath::Max(Got.GetSize().X, 1.0),
					HullBox.GetSize().Y / FMath::Max(Got.GetSize().Y, 1.0));

				bHullsFit = bInside && Spans >= 0.5;
			}

			const bool bOk = Hulls == Expected && Error <= 1.0 && bHullsFit;

			UE_LOG(LogSpaceMMOAuthoring, Display,
				TEXT("Settlements: %-28s %4d hulls (manifest %4d), bounds off by %.2f cm, hulls span %s cm%s"),
				*Name, Hulls, Expected, Error,
				Hulls > 0 ? *HullBox.GetSize().ToCompactString() : TEXT("nothing"),
				bOk ? TEXT("") : TEXT("  <-- WRONG"));

			Problems += bOk ? 0 : 1;
		}

		return Problems;
	}

	/** Fills each mesh's material slots from the instances, by the slot name Blender gave it. */
	int32 AssignMaterials(const TArray<UStaticMesh*>& Meshes, const TMap<FName, UMaterialInstanceConstant*>& Made)
	{
		int32 Problems = 0;

		for (UStaticMesh* Mesh : Meshes)
		{
			const TArray<FStaticMaterial>& Slots = Mesh->GetStaticMaterials();

			for (int32 Index = 0; Index < Slots.Num(); ++Index)
			{
				const FName Slot = Slots[Index].ImportedMaterialSlotName.IsNone()
					? Slots[Index].MaterialSlotName
					: Slots[Index].ImportedMaterialSlotName;

				UMaterialInstanceConstant* const* Instance = Made.Find(Slot);

				if (Instance == nullptr)
				{
					UE_LOG(LogSpaceMMOAuthoring, Error,
						TEXT("Settlements: %s slot %d is '%s', which the manifest has no material for."),
						*Mesh->GetName(), Index, *Slot.ToString());

					++Problems;

					continue;
				}

				Mesh->SetMaterial(Index, *Instance);
			}

			// Simple collision answers the walk sweep and the ship sweep, which ask for nothing else.
			if (Mesh->GetBodySetup() != nullptr)
			{
				Mesh->GetBodySetup()->CollisionTraceFlag = CTF_UseDefault;
			}

			Mesh->PostEditChange();

			if (!Save(Mesh))
			{
				++Problems;
			}
		}

		return Problems;
	}

	/**
	 * Builds the Blueprint from scratch: a mesh component per mesh, a berth per pad, a service point
	 * per counter, and the settlement's numbers on its defaults.
	 */
	UBlueprint* BuildBlueprint(
		const FSettlement& Settlement,
		const TArray<UStaticMesh*>& Meshes,
		const TSharedPtr<FJsonObject>& Manifest,
		const FString& FbxBase,
		int32& Problems)
	{
		UClass* Parent = LoadObject<UClass>(nullptr, SettlementClassPath);
		UClass* BerthClass = LoadObject<UClass>(nullptr, BerthClassPath);
		UClass* ServiceClass = LoadObject<UClass>(nullptr, ServicePointClassPath);

		if (Parent == nullptr || BerthClass == nullptr || ServiceClass == nullptr)
		{
			UE_LOG(LogSpaceMMOAuthoring, Error, TEXT("Settlements: the settlement classes are missing; is SpaceMMOBackend built?"));

			++Problems;

			return nullptr;
		}

		const FString PackageName = FString(Settlement.ContentFolder) / Settlement.Blueprint;
		UPackage* Package = CreatePackage(*PackageName);

		// Its whole contents are replaced; see the menus commandlet for why this matters on a rebuild.
		Package->MarkAsFullyLoaded();

		UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
			Parent, Package, FName(Settlement.Blueprint), BPTYPE_Normal);

		if (Blueprint == nullptr || Blueprint->SimpleConstructionScript == nullptr)
		{
			UE_LOG(LogSpaceMMOAuthoring, Error, TEXT("Settlements: could not create %s."), *PackageName);

			++Problems;

			return nullptr;
		}

		USimpleConstructionScript* Script = Blueprint->SimpleConstructionScript;

		for (UStaticMesh* Mesh : Meshes)
		{
			// "Admin", "Wall": the manifest's name without its prefix, so the component list reads as a city.
			const FString ComponentName = ManifestName(Mesh, FbxBase).Replace(TEXT("SM_Borlash_"), TEXT(""));
			USCS_Node* Node = Script->CreateNode(UStaticMeshComponent::StaticClass(), FName(*ComponentName));
			UStaticMeshComponent* Template = Cast<UStaticMeshComponent>(Node->ComponentTemplate);

			Template->SetStaticMesh(Mesh);
			Template->SetMobility(EComponentMobility::Movable);

			// Queried, never simulated, like the ship and the deposits (task 130): where things are comes
			// from the flight and walk models, and a physics body would be a second opinion. Blocking
			// everything, so the character's sweep, the ship's sweep and the camera all stop here.
			Template->SetCollisionProfileName(TEXT("BlockAll"));
			Template->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			Template->SetGenerateOverlapEvents(false);

			Script->AddNode(Node);
		}

		int32 Berths = 0;

		for (const TSharedPtr<FJsonValue>& Value : Manifest->GetArrayField(TEXT("berths")))
		{
			const TSharedPtr<FJsonObject> Row = Value->AsObject();
			const FString Key = Row->GetStringField(TEXT("key"));
			USCS_Node* Node = Script->CreateNode(BerthClass, FName(*(TEXT("Berth_") + Key)));
			USceneComponent* Template = Cast<USceneComponent>(Node->ComponentTemplate);

			Template->SetRelativeLocation_Direct(ToUnreal(ReadVector(Row, TEXT("pad_m"))));

			const bool bSet =
				SetString(Template, TEXT("BerthKey"), Key)
				&& SetString(Template, TEXT("DisplayName"), Row->GetStringField(TEXT("name")))
				&& SetString(Template, TEXT("ShortName"), Row->GetStringField(TEXT("short_name")))
				&& SetDouble(Template, TEXT("PadRadiusMetres"), Row->GetNumberField(TEXT("pad_radius_m")))
				&& SetVector(Template, TEXT("DockCentre"), ToUnreal(ReadVector(Row, TEXT("dock_centre_m"))))
				&& SetDouble(Template, TEXT("DockRadiusMetres"), Row->GetNumberField(TEXT("dock_radius_m")));

			Problems += bSet ? 0 : 1;

			Script->AddNode(Node);
			++Berths;
		}

		int32 Points = 0;

		for (const TSharedPtr<FJsonValue>& Value : Manifest->GetArrayField(TEXT("service_points")))
		{
			const TSharedPtr<FJsonObject> Row = Value->AsObject();
			const FString Name = FString::Printf(TEXT("Service_%02d_%s_%s"), Points,
				*Row->GetStringField(TEXT("building")), *Row->GetStringField(TEXT("service")));
			USCS_Node* Node = Script->CreateNode(ServiceClass, FName(*Name));
			USceneComponent* Template = Cast<USceneComponent>(Node->ComponentTemplate);

			// Facing is measured anticlockwise from east in Blender; Unreal's Y is flipped, so its yaw
			// turns the other way.
			Template->SetRelativeLocation_Direct(ToUnreal(ReadVector(Row, TEXT("stand_m"))));
			Template->SetRelativeRotation_Direct(FRotator(0.0, -Row->GetNumberField(TEXT("facing_deg")), 0.0));

			TArray<FString> Skills;
			const TArray<TSharedPtr<FJsonValue>>* SkillValues = nullptr;

			if (Row->TryGetArrayField(TEXT("skills"), SkillValues) && SkillValues != nullptr)
			{
				for (const TSharedPtr<FJsonValue>& Skill : *SkillValues)
				{
					Skills.Add(Skill->AsString());
				}
			}

			bool bAvailable = true;
			Row->TryGetBoolField(TEXT("available"), bAvailable);

			FString Note;
			Row->TryGetStringField(TEXT("note"), Note);

			const bool bSet =
				SetString(Template, TEXT("Building"), Row->GetStringField(TEXT("building")))
				&& SetString(Template, TEXT("Service"), Row->GetStringField(TEXT("service")))
				&& SetString(Template, TEXT("Label"), Row->GetStringField(TEXT("label")))
				&& SetDouble(Template, TEXT("ReachMetres"), Row->GetNumberField(TEXT("reach_m")))
				&& SetStrings(Template, TEXT("Skills"), Skills)
				&& SetBool(Template, TEXT("bAvailable"), bAvailable)
				&& SetString(Template, TEXT("Note"), Note);

			Problems += bSet ? 0 : 1;

			Script->AddNode(Node);
			++Points;
		}

		FKismetEditorUtilities::CompileBlueprint(Blueprint);

		if (Blueprint->Status == BS_Error || Blueprint->GeneratedClass == nullptr)
		{
			UE_LOG(LogSpaceMMOAuthoring, Error, TEXT("Settlements: %s did not compile."), *PackageName);

			++Problems;

			return Blueprint;
		}

		// The settlement's own numbers, on the compiled class's defaults.
		UObject* Defaults = Blueprint->GeneratedClass->GetDefaultObject();
		const TSharedPtr<FJsonObject> Airspace = Manifest->GetObjectField(TEXT("airspace"));
		const TSharedPtr<FJsonObject> Disembark = Airspace->GetObjectField(TEXT("no_disembark"));

		const bool bDefaults =
			SetDouble(Defaults, TEXT("FloorMetres"), Manifest->GetNumberField(TEXT("floor_m")))
			&& SetDouble(Defaults, TEXT("NoFlyRadiusMetres"), Airspace->GetNumberField(TEXT("no_fly_radius_m")))
			&& SetDouble(Defaults, TEXT("NoFlyCeilingMetres"), Airspace->GetNumberField(TEXT("no_fly_ceiling_m")))
			&& SetDouble(Defaults, TEXT("PlatformHalfWidthMetres"), Disembark->GetNumberField(TEXT("square_half_m")));

		Problems += bDefaults ? 0 : 1;

		Blueprint->MarkPackageDirty();
		FAssetRegistryModule::AssetCreated(Blueprint);

		if (!Save(Blueprint))
		{
			++Problems;
		}

		// Counted off the built class's construction script, not off the loops above.
		int32 MeshNodes = 0;
		int32 BerthNodes = 0;
		int32 PointNodes = 0;

		for (USCS_Node* Node : Script->GetAllNodes())
		{
			const UClass* Class = Node->ComponentClass;

			MeshNodes += Class == UStaticMeshComponent::StaticClass() ? 1 : 0;
			BerthNodes += Class == BerthClass ? 1 : 0;
			PointNodes += Class == ServiceClass ? 1 : 0;
		}

		const int32 WantMeshes = Manifest->GetArrayField(TEXT("meshes")).Num();
		const int32 WantBerths = Manifest->GetArrayField(TEXT("berths")).Num();
		const int32 WantPoints = Manifest->GetArrayField(TEXT("service_points")).Num();

		UE_LOG(LogSpaceMMOAuthoring, Display,
			TEXT("Settlements: %s has %d mesh components (manifest %d), %d berths (%d), %d service points (%d)."),
			*PackageName, MeshNodes, WantMeshes, BerthNodes, WantBerths, PointNodes, WantPoints);

		if (MeshNodes != WantMeshes || BerthNodes != WantBerths || PointNodes != WantPoints)
		{
			++Problems;
		}

		return Blueprint;
	}
}

USpaceMMOImportSettlementsCommandlet::USpaceMMOImportSettlementsCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

int32 USpaceMMOImportSettlementsCommandlet::Main(const FString& Params)
{
	using namespace SpaceMMOImportSettlements;

	int32 Problems = 0;

	for (const FSettlement& Settlement : Settlements)
	{
		const FString Raw = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / Settlement.RawFolder);
		const FString Fbx = Raw / Settlement.Fbx;
		const FString ManifestPath = Raw / Settlement.Manifest;

		FString Text;
		TSharedPtr<FJsonObject> Manifest;

		const bool bRead = FFileHelper::LoadFileToString(Text, *ManifestPath);
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);

		if (!bRead || !FJsonSerializer::Deserialize(Reader, Manifest) || !Manifest.IsValid())
		{
			UE_LOG(LogSpaceMMOAuthoring, Error, TEXT("Settlements: could not read %s."), *ManifestPath);

			++Problems;

			continue;
		}

		const FString Folder = Settlement.ContentFolder;

		UE_LOG(LogSpaceMMOAuthoring, Display, TEXT("Settlements: %s from %s"), Settlement.Name, *Fbx);

		UMaterial* Base = BaseMaterial(FString(TEXT("/Game/Stations/Shared")), Problems);
		const TMap<FName, UMaterialInstanceConstant*> Made = Base != nullptr
			? Instances(Manifest, Base, Folder / TEXT("Materials"), Problems)
			: TMap<FName, UMaterialInstanceConstant*>();

		const TArray<UStaticMesh*> Meshes = ImportMeshes(Fbx, Folder / TEXT("Meshes"), Problems);

		const FString FbxBase = FPaths::GetBaseFilename(Fbx);

		Problems += AssignMaterials(Meshes, Made);
		Problems += CheckMeshes(Meshes, Manifest, FbxBase);

		BuildBlueprint(Settlement, Meshes, Manifest, FbxBase, Problems);
	}

	UE_LOG(LogSpaceMMOAuthoring, Display, TEXT("Settlements: %d problem(s). Result: %s"),
		Problems, Problems == 0 ? TEXT("OK") : TEXT("FAILED"));

	return Problems == 0 ? 0 : 1;
}
