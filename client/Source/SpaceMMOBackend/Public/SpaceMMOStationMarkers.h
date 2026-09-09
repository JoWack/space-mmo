#pragma once

#include "CoreMinimal.h"
#include "SpaceMMOCoordinates.h"
#include "SpaceMMOPlanet.h"

/**
 * Which stations to point at, and which to leave alone.
 *
 * <strong>Task 160.</strong> Nothing on screen said where a station was. Joe landed 423 m from
 * Terra Outpost, pressed G, and got a refusal that was correct in every particular and impossible
 * to act on: the station is a 25 m cube, the horizon at rest on a 20 km world is about 285 m, and a
 * ridge was in the way. The docked overlay cannot help somebody get there and the crosshair marker
 * is the velocity vector, so the refusal message was the game's only rangefinder.
 *
 * <strong>Pure, and over plain values rather than actors</strong>, for the reason
 * <c>FSpaceMMOBodySelection</c> is: the interesting cases here are geometric — a station just over
 * the limb, a deep-space dock behind the planet — and none of them need a world, a widget or a
 * spawn to state.
 */

/** One station, reduced to what choosing a marker needs. */
struct SPACEMMOBACKEND_API FSpaceMMOMarkerStation
{
	int32 Id = 0;

	/** The body it stands on, or 0 for a deep-space station. */
	int32 BodyId = 0;

	bool bOnBody = false;

	/** Where it actually is, already resolved onto the terrain for a station on a body. */
	FSystemCoordinate Position;

	double DockingRangeKilometres = 0.1;
};

/** The body the viewer is at, when there is one. */
struct SPACEMMOBACKEND_API FSpaceMMOMarkerBody
{
	int32 Id = 0;

	FSystemCoordinate Centre;

	double RadiusKilometres = 0.0;

	/** False when there is no planet nearby at all, which is a working state out in deep space. */
	bool bValid = false;
};

/**
 * One chosen station, as both the chevron and the readout need it.
 *
 * <strong>Built once and read twice.</strong> The crosshair draws these and the readout names one
 * of them, and they must never disagree about which — so the selection happens in one place,
 * <c>USpaceMMODockingComponent::BuildStationMarkers</c>, and both widgets read the result rather
 * than each running the rule for itself.
 */
struct SPACEMMOBACKEND_API FSpaceMMOStationMarkerView
{
	FString Name;

	/** The world it stands on, for the readout's far form. Empty for a deep-space station. */
	FString BodyName;

	FSystemCoordinate Position;

	double DistanceKilometres = 0.0;

	double DockingRangeKilometres = 0.1;

	bool bOnBody = false;
};

/**
 * What the readout says about the station it names.
 *
 * <strong>One definition, two readouts.</strong> The ship's flight readout and the on-foot readout
 * are separate widgets with separate text, and the marker persists across both -- so the wording
 * lives here rather than in either of them, where the two would drift and only somebody who walked
 * out of a ship would ever see it.
 *
 * Beside the geometry rather than in a header of its own because it is the same feature: what to
 * point at, and what to say about it.
 */
struct SPACEMMOBACKEND_API FSpaceMMOStationLine
{
	/** Past this, metres stop meaning anything and the line names the world instead. */
	static constexpr double FarKilometres = 10.0;

	/**
	 * Just the distance, for a chevron's label.
	 *
	 * Metres up close and kilometres beyond, on the same threshold the readout switches at, so the
	 * two never disagree about whether something is far away.
	 */
	static FString Short(const double DistanceKilometres)
	{
		return DistanceKilometres >= FarKilometres
			? FString::Printf(TEXT("%.0f km"), DistanceKilometres)
			: FString::Printf(TEXT("%.0f m"), DistanceKilometres * 1000.0);
	}

	/**
	 * The station line, or empty when there is nothing to name.
	 *
	 * Empty rather than "none": a line reading "Station: none" is a row of dead pixels on every
	 * frame of a game that is mostly flying between things.
	 */
	static FString Format(
		const FString& StationName,
		const double DistanceKilometres,
		const double DockingRangeKilometres,
		const bool bOnBody,
		const FString& BodyName)
	{
		if (StationName.IsEmpty())
		{
			return FString();
		}

		if (DistanceKilometres >= FarKilometres)
		{
			// Far away: the distance in kilometres, and where it is. The world is worth naming here
			// and not up close, because at 118 km the useful question is which planet to fly to --
			// and for a deep-space dock the answer is "none", which is exactly what a pilot needs
			// to know before pointing at a planet expecting to land.
			const FString Where = bOnBody
				? (BodyName.IsEmpty() ? FString() : FString::Printf(TEXT("  ·  at %s"), *BodyName))
				: FString(TEXT("  ·  deep space"));

			return FString::Printf(
				TEXT("%s  %.0f km%s"), *StationName, DistanceKilometres, *Where);
		}

		const double Metres = DistanceKilometres * 1000.0;

		if (DistanceKilometres <= DockingRangeKilometres)
		{
			return FString::Printf(TEXT("%s  %.0f m  ·  READY"), *StationName, Metres);
		}

		return FString::Printf(
			TEXT("%s  %.0f m  ·  dock at %.0f m"),
			*StationName, Metres, DockingRangeKilometres * 1000.0);
	}
};

struct SPACEMMOBACKEND_API FSpaceMMOStationMarkers
{
	/**
	 * True when a point on a body is on the side of it facing the viewer.
	 *
	 * <strong>The exact limb test, not an approximation.</strong> A point P on a body centred at C
	 * is visible from V precisely when the vector out to it and the vector on to the viewer form an
	 * acute angle — at the limb they are perpendicular and this goes to zero. Checked against the
	 * Capital: from 40 km up the visible cap reaches <c>acos(R/D)</c> = 70.5 degrees from the
	 * sub-point, and a station at 90 degrees is correctly hidden.
	 *
	 * It costs one dot product and needs no trigonometry, and because P is the station's real
	 * position rather than a point on the nominal sphere, terrain height is handled for free.
	 */
	static bool FacesViewer(
		const FSystemCoordinate& Station,
		const FSystemCoordinate& Centre,
		const FSystemCoordinate& Viewer)
	{
		const FVector Out = Station.Kilometres - Centre.Kilometres;
		const FVector On = Viewer.Kilometres - Station.Kilometres;

		return FVector::DotProduct(Out, On) > 0.0;
	}

	/**
	 * True when a body sits between the viewer and a station, hiding it.
	 *
	 * For deep-space stations, which belong to no body and so have no near side of their own.
	 * Deepdock is 32.6 km from the Capital, which means it spends a good part of any orbit behind
	 * it, and a chevron pointing confidently through a planet is worse than none.
	 *
	 * The segment from viewer to station is tested against the sphere: the nearest approach of that
	 * segment to the centre, clamped to the segment's ends so that a body behind the viewer or
	 * beyond the station never counts as occluding.
	 */
	static bool IsHiddenBehind(
		const FSystemCoordinate& Station,
		const FSystemCoordinate& Viewer,
		const FSystemCoordinate& Centre,
		const double RadiusKilometres)
	{
		const FVector Along = Station.Kilometres - Viewer.Kilometres;

		const double LengthSquared = Along.SizeSquared();

		if (LengthSquared <= 0.0)
		{
			return false;
		}

		const FVector ToCentre = Centre.Kilometres - Viewer.Kilometres;

		// Clamped, so only a body actually between the two can hide anything.
		const double Fraction = FMath::Clamp(
			FVector::DotProduct(ToCentre, Along) / LengthSquared, 0.0, 1.0);

		const FVector Nearest = Viewer.Kilometres + (Along * Fraction);

		return (Nearest - Centre.Kilometres).Size() < RadiusKilometres;
	}

	/**
	 * The nearest placed station at any distance, and how far away it is.
	 *
	 * <strong>The single definition of "the nearest station", so the HUD and the docking refusal
	 * can never disagree.</strong> `USpaceMMODockingComponent::NearestStation` answers with this,
	 * which is what stops a marker saying one thing while "nearest is Terra Outpost at 266 m" says
	 * another — the kind of pair that only ever gets found by somebody standing in the wrong place.
	 */
	static int32 NearestPlaced(
		TArrayView<const FSpaceMMOMarkerStation> Stations,
		const FSystemCoordinate& Viewer,
		double& OutKilometres)
	{
		int32 Nearest = INDEX_NONE;

		double NearestDistance = TNumericLimits<double>::Max();

		for (int32 Index = 0; Index < Stations.Num(); ++Index)
		{
			const double Distance =
				(Viewer.Kilometres - Stations[Index].Position.Kilometres).Size();

			if (Distance < NearestDistance)
			{
				NearestDistance = Distance;
				Nearest = Index;
			}
		}

		OutKilometres = Nearest != INDEX_NONE ? NearestDistance : 0.0;

		return Nearest;
	}

	/**
	 * Which stations to draw a chevron for, and which one the readout names.
	 *
	 * <strong>Two modes, decided by proximity</strong>, which is Joe's rule and it is about what the
	 * marker is for at the time:
	 *
	 * - <strong>Orbital</strong> — choosing where to go. Every station on the near side of the body
	 *   below, plus any deep-space station the body is not hiding. Stations on *other* bodies are
	 *   left out: they belong to a world that is not the one being looked at.
	 * - <strong>Atmospheric, surface, and on foot</strong> — going there. The closest station on the
	 *   body the viewer is actually at, and nothing else. A chevron pointing 190 km at Terra while
	 *   somebody stands on the Capital is noise, because they cannot walk to it.
	 *
	 * Docked draws nothing at all: the station overlay has taken the screen, and the answer to
	 * "where is the station" is "you are in it".
	 *
	 * Unplaced stations never appear. One is nowhere rather than at the origin, and a chevron
	 * pointing at the centre of the star system is a confident wrong answer.
	 */
	static void Select(
		TArrayView<const FSpaceMMOMarkerStation> Stations,
		const FSystemCoordinate& Viewer,
		const EPlanetProximity Proximity,
		const FSpaceMMOMarkerBody& Body,
		const bool bDocked,
		TArray<int32>& OutMarked,
		int32& OutNamed)
	{
		OutMarked.Reset();
		OutNamed = INDEX_NONE;

		if (bDocked)
		{
			return;
		}

		if (Proximity == EPlanetProximity::Orbital)
		{
			double Best = TNumericLimits<double>::Max();

			for (int32 Index = 0; Index < Stations.Num(); ++Index)
			{
				const FSpaceMMOMarkerStation& Station = Stations[Index];

				if (!ShownInOrbit(Station, Viewer, Body))
				{
					continue;
				}

				OutMarked.Add(Index);

				const double Distance =
					(Viewer.Kilometres - Station.Position.Kilometres).Size();

				if (Distance < Best)
				{
					Best = Distance;
					OutNamed = Index;
				}
			}

			return;
		}

		// Close in: the nearest station on this body, and only this body.
		if (!Body.bValid)
		{
			return;
		}

		double Best = TNumericLimits<double>::Max();

		for (int32 Index = 0; Index < Stations.Num(); ++Index)
		{
			const FSpaceMMOMarkerStation& Station = Stations[Index];

			if (!Station.bOnBody || Station.BodyId != Body.Id)
			{
				continue;
			}

			const double Distance = (Viewer.Kilometres - Station.Position.Kilometres).Size();

			if (Distance < Best)
			{
				Best = Distance;
				OutNamed = Index;
			}
		}

		if (OutNamed != INDEX_NONE)
		{
			OutMarked.Add(OutNamed);
		}
	}

private:
	/** Whether one station belongs on screen from orbit. */
	static bool ShownInOrbit(
		const FSpaceMMOMarkerStation& Station,
		const FSystemCoordinate& Viewer,
		const FSpaceMMOMarkerBody& Body)
	{
		// No body below: nothing can be occluded and nothing has a near side, so every station is
		// as visible as it is ever going to be.
		if (!Body.bValid)
		{
			return true;
		}

		if (Station.bOnBody)
		{
			return Station.BodyId == Body.Id
				&& FacesViewer(Station.Position, Body.Centre, Viewer);
		}

		return !IsHiddenBehind(Station.Position, Viewer, Body.Centre, Body.RadiusKilometres);
	}
};
