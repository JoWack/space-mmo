#pragma once

#include "CoreMinimal.h"
#include "SpaceMMOCoordinates.generated.h"

/**
 * Scale and unit constants for the three-tier coordinate model (ADR-0001).
 */
namespace SpaceMMO::Coordinates
{
	/**
	 * Unreal works in centimetres, and system space is expressed in kilometres.
	 */
	inline constexpr double CentimetresPerKilometre = 100000.0;

	/**
	 * Solar systems are modelled at 1:10.
	 *
	 * An Earth-analog becomes ~637 km in radius, which sits comfortably inside Large World
	 * Coordinates and keeps travel times playable. True scale is unusable in both directions at
	 * once: the coordinates do not fit, and the emptiness between objects is measured in hours.
	 *
	 * IMPORTANT: this is applied once, when content is authored or generated. System space stores
	 * already-scaled kilometres, so converting a position to render space is a plain unit change
	 * with no scale factor in it. Scaling in the conversion instead would put a multiply on the
	 * hot path for every replicated position, and — far worse — make double-applying it a silent
	 * bug that shrinks the universe by ten each time someone converts twice.
	 */
	inline constexpr double UniverseScale = 0.1;

	/**
	 * How far from the origin local physics is trusted.
	 *
	 * Chaos loses precision long before LWC runs out of range, so grids rebase well inside the
	 * numeric limit. This is a physics budget, not a numeric one.
	 */
	inline constexpr double LocalSpaceLimitCentimetres = 2000000.0; // 20 km

	/** True kilometres per unit of galaxy space. One light year, near enough. */
	inline constexpr double KilometresPerGalaxyUnit = 9.46e12;
}

/**
 * A position in galaxy space: which star system, not where inside one.
 *
 * Stored as int64 and never converted into an Unreal world position. A galaxy is far larger than
 * Large World Coordinates can express, so galaxy space exists only as data — the backend places
 * systems in it, and the engine never sees it (ADR-0001).
 *
 * Integers rather than doubles because these values are compared for equality, used as map keys,
 * and fed into deterministic generation, none of which tolerate floating-point drift.
 */
USTRUCT(BlueprintType)
struct SPACEMMOCORE_API FGalaxyCoordinate
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Coordinates")
	int64 X = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Coordinates")
	int64 Y = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Coordinates")
	int64 Z = 0;

	FGalaxyCoordinate() = default;

	FGalaxyCoordinate(const int64 InX, const int64 InY, const int64 InZ)
		: X(InX), Y(InY), Z(InZ)
	{
	}

	/**
	 * Squared distance to another system, in galaxy units.
	 *
	 * Squared and integral so that range comparisons — "which systems are within warp range?" —
	 * stay exact. Taking a square root would introduce rounding into a question with a definite
	 * yes-or-no answer.
	 */
	int64 DistanceSquaredTo(const FGalaxyCoordinate& Other) const
	{
		const int64 DX = X - Other.X;
		const int64 DY = Y - Other.Y;
		const int64 DZ = Z - Other.Z;

		return (DX * DX) + (DY * DY) + (DZ * DZ);
	}

	/** Distance in galaxy units, for display. */
	double DistanceTo(const FGalaxyCoordinate& Other) const
	{
		return FMath::Sqrt(static_cast<double>(DistanceSquaredTo(Other)));
	}

	bool operator==(const FGalaxyCoordinate& Other) const
	{
		return X == Other.X && Y == Other.Y && Z == Other.Z;
	}

	bool operator!=(const FGalaxyCoordinate& Other) const { return !(*this == Other); }

	FString ToString() const
	{
		return FString::Printf(TEXT("(%lld, %lld, %lld)"), X, Y, Z);
	}
};

FORCEINLINE uint32 GetTypeHash(const FGalaxyCoordinate& Coordinate)
{
	return HashCombine(
		HashCombine(GetTypeHash(Coordinate.X), GetTypeHash(Coordinate.Y)),
		GetTypeHash(Coordinate.Z));
}

/**
 * A position within one star system, in kilometres from the system's barycentre.
 *
 * Double precision, which Large World Coordinates gives for free in UE5. This is the authoritative
 * position of anything large enough to be seen from far away — planets, stations, ships — and it is
 * what gets replicated.
 *
 * A distinct type rather than a bare FVector so that kilometres and centimetres cannot be mixed by
 * accident. That confusion is silent, produces objects a hundred thousand times too far away, and
 * is exactly the sort of mistake the type system should be catching.
 */
USTRUCT(BlueprintType)
struct SPACEMMOCORE_API FSystemCoordinate
{
	GENERATED_BODY()

	/** Kilometres from the system barycentre. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Coordinates")
	FVector Kilometres = FVector::ZeroVector;

	FSystemCoordinate() = default;

	explicit FSystemCoordinate(const FVector& InKilometres)
		: Kilometres(InKilometres)
	{
	}

	FSystemCoordinate(const double InX, const double InY, const double InZ)
		: Kilometres(InX, InY, InZ)
	{
	}

	/**
	 * Converts to a render position relative to a grid origin.
	 *
	 * The whole point of the tier split: the subtraction happens in kilometres at double
	 * precision, so only the small residual is ever scaled up into centimetres. Converting first
	 * and subtracting afterwards would throw away the precision this exists to preserve.
	 */
	FVector ToLocalCentimetres(const FSystemCoordinate& GridOrigin) const
	{
		return (Kilometres - GridOrigin.Kilometres)
			* SpaceMMO::Coordinates::CentimetresPerKilometre;
	}

	/** Rebuilds a system position from a local render position and its grid origin. */
	static FSystemCoordinate FromLocalCentimetres(
		const FVector& LocalCentimetres, const FSystemCoordinate& GridOrigin)
	{
		const FVector OffsetKilometres =
			LocalCentimetres / SpaceMMO::Coordinates::CentimetresPerKilometre;

		return FSystemCoordinate(GridOrigin.Kilometres + OffsetKilometres);
	}

	/** Distance to another position, in kilometres. */
	double DistanceTo(const FSystemCoordinate& Other) const
	{
		return FVector::Dist(Kilometres, Other.Kilometres);
	}

	double DistanceSquaredTo(const FSystemCoordinate& Other) const
	{
		return FVector::DistSquared(Kilometres, Other.Kilometres);
	}

	/**
	 * True if a position is close enough to render in a grid without physics degrading.
	 *
	 * The test that decides when a grid must rebase.
	 */
	bool IsWithinLocalSpaceOf(const FSystemCoordinate& GridOrigin) const
	{
		return ToLocalCentimetres(GridOrigin).SizeSquared()
			<= FMath::Square(SpaceMMO::Coordinates::LocalSpaceLimitCentimetres);
	}

	FSystemCoordinate operator+(const FSystemCoordinate& Other) const
	{
		return FSystemCoordinate(Kilometres + Other.Kilometres);
	}

	FSystemCoordinate operator-(const FSystemCoordinate& Other) const
	{
		return FSystemCoordinate(Kilometres - Other.Kilometres);
	}

	bool operator==(const FSystemCoordinate& Other) const
	{
		return Kilometres == Other.Kilometres;
	}

	bool operator!=(const FSystemCoordinate& Other) const { return !(*this == Other); }

	FString ToString() const
	{
		return FString::Printf(
			TEXT("(%.3f, %.3f, %.3f) km"), Kilometres.X, Kilometres.Y, Kilometres.Z);
	}
};

/**
 * Blueprint-facing conversions, so content can work in these units without touching C++.
 */
UCLASS()
class SPACEMMOCORE_API USpaceMMOCoordinateLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Converts a system position into a render position relative to a grid origin. */
	UFUNCTION(BlueprintPure, Category = "SpaceMMO|Coordinates")
	static FVector SystemToLocal(
		const FSystemCoordinate& Position, const FSystemCoordinate& GridOrigin)
	{
		return Position.ToLocalCentimetres(GridOrigin);
	}

	/** Rebuilds a system position from a render position and its grid origin. */
	UFUNCTION(BlueprintPure, Category = "SpaceMMO|Coordinates")
	static FSystemCoordinate LocalToSystem(
		const FVector& LocalCentimetres, const FSystemCoordinate& GridOrigin)
	{
		return FSystemCoordinate::FromLocalCentimetres(LocalCentimetres, GridOrigin);
	}

	/** Distance between two system positions, in kilometres. */
	UFUNCTION(BlueprintPure, Category = "SpaceMMO|Coordinates")
	static double DistanceKilometres(const FSystemCoordinate& A, const FSystemCoordinate& B)
	{
		return A.DistanceTo(B);
	}

	/** True if a position can be rendered in a grid without physics degrading. */
	UFUNCTION(BlueprintPure, Category = "SpaceMMO|Coordinates")
	static bool IsWithinLocalSpace(
		const FSystemCoordinate& Position, const FSystemCoordinate& GridOrigin)
	{
		return Position.IsWithinLocalSpaceOf(GridOrigin);
	}

	/** The scaled radius, in kilometres, of a body given its true radius. */
	UFUNCTION(BlueprintPure, Category = "SpaceMMO|Coordinates")
	static double ScaledRadiusKilometres(const double TrueRadiusKilometres)
	{
		return TrueRadiusKilometres * SpaceMMO::Coordinates::UniverseScale;
	}
};
