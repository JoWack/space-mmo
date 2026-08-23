#include "SpaceMMOStationSettings.h"

#include "Engine/StaticMesh.h"

TSoftObjectPtr<UStaticMesh> FStationAppearance::MeshFor(
	const USpaceMMOStationSettings& Settings, const FString& Key, const FString& Kind)
{
	// The station's own key first. An override that the general case could beat would not be one.
	if (!Key.IsEmpty())
	{
		if (const TSoftObjectPtr<UStaticMesh>* const ByKey = Settings.MeshesByKey.Find(Key))
		{
			if (!ByKey->IsNull())
			{
				return *ByKey;
			}
		}
	}

	if (!Kind.IsEmpty())
	{
		if (const TSoftObjectPtr<UStaticMesh>* const ByKind = Settings.MeshesByKind.Find(Kind))
		{
			if (!ByKind->IsNull())
			{
				return *ByKind;
			}
		}
	}

	return nullptr;
}

double FStationAppearance::SizeMetresFor(
	const USpaceMMOStationSettings& Settings, const FString& Kind)
{
	if (!Kind.IsEmpty())
	{
		if (const double* const ByKind = Settings.SizeMetresByKind.Find(Kind))
		{
			// A zero or negative size is not a station anybody can see, and taking it literally
			// would draw nothing while the log said it had been placed.
			if (*ByKind > 0.0)
			{
				return *ByKind;
			}
		}
	}

	return Settings.DefaultSizeMetres > 0.0 ? Settings.DefaultSizeMetres : 25.0;
}

double FStationAppearance::UniformScaleForSize(
	const FVector& LocalBoxExtent, const double TargetSizeCentimetres)
{
	// The largest dimension, so nothing sticks out past the size that was asked for. Extents are
	// half-widths, hence the doubling.
	const double Largest = LocalBoxExtent.GetAbsMax() * 2.0;

	if (Largest <= UE_DOUBLE_SMALL_NUMBER || TargetSizeCentimetres <= 0.0)
	{
		return 1.0;
	}

	return TargetSizeCentimetres / Largest;
}
