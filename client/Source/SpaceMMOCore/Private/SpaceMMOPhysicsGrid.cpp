#include "SpaceMMOPhysicsGrid.h"

FPhysicsGridRegistry::FGridId FPhysicsGridRegistry::AddRoot(
	const FName DebugName, const FSystemCoordinate& Origin, const FQuat& Rotation)
{
	FGrid Grid;
	Grid.DebugName = DebugName;
	Grid.ParentId = InvalidGrid;
	Grid.RootOrigin = Origin;
	Grid.LocalRotation = Rotation.GetNormalized();

	return Grids.Add(Grid);
}

FPhysicsGridRegistry::FGridId FPhysicsGridRegistry::AddChild(
	const FName DebugName,
	const FGridId ParentId,
	const FVector& OffsetCentimetres,
	const FQuat& Rotation)
{
	checkf(IsValidGrid(ParentId), TEXT("AddChild called with an unknown parent grid."));

	// Children are only ever attached to grids that already exist, so the structure is a tree by
	// construction and no cycle check is needed here. Nothing reparents a grid; a cycle could only
	// come from adding that ability later, which is why ResolveWorldPose still guards its walk.
	FGrid Grid;
	Grid.DebugName = DebugName;
	Grid.ParentId = ParentId;
	Grid.LocalOffsetCentimetres = OffsetCentimetres;
	Grid.LocalRotation = Rotation.GetNormalized();

	return Grids.Add(Grid);
}

bool FPhysicsGridRegistry::IsValidGrid(const FGridId Id) const
{
	return Grids.IsValidIndex(Id);
}

FPhysicsGridRegistry::FGridId FPhysicsGridRegistry::GetParent(const FGridId Id) const
{
	return IsValidGrid(Id) ? Grids[Id].ParentId : InvalidGrid;
}

FName FPhysicsGridRegistry::GetDebugName(const FGridId Id) const
{
	return IsValidGrid(Id) ? Grids[Id].DebugName : NAME_None;
}

int32 FPhysicsGridRegistry::GetDepth(const FGridId Id) const
{
	if (!IsValidGrid(Id))
	{
		return INDEX_NONE;
	}

	int32 Depth = 0;

	for (FGridId Current = Grids[Id].ParentId;
		Current != InvalidGrid && Depth < MaxDepth;
		Current = Grids[Current].ParentId)
	{
		++Depth;
	}

	return Depth;
}

bool FPhysicsGridRegistry::IsDescendantOf(const FGridId Id, const FGridId PossibleAncestor) const
{
	if (!IsValidGrid(Id) || !IsValidGrid(PossibleAncestor))
	{
		return false;
	}

	int32 Steps = 0;

	for (FGridId Current = Grids[Id].ParentId;
		Current != InvalidGrid && Steps < MaxDepth;
		Current = Grids[Current].ParentId, ++Steps)
	{
		if (Current == PossibleAncestor)
		{
			return true;
		}
	}

	return false;
}

void FPhysicsGridRegistry::SetLocalOffset(const FGridId Id, const FVector& OffsetCentimetres)
{
	if (IsValidGrid(Id))
	{
		Grids[Id].LocalOffsetCentimetres = OffsetCentimetres;
	}
}

void FPhysicsGridRegistry::SetLocalRotation(const FGridId Id, const FQuat& Rotation)
{
	if (IsValidGrid(Id))
	{
		Grids[Id].LocalRotation = Rotation.GetNormalized();
	}
}

void FPhysicsGridRegistry::SetRootOrigin(const FGridId Id, const FSystemCoordinate& Origin)
{
	if (IsValidGrid(Id))
	{
		Grids[Id].RootOrigin = Origin;
	}
}

FPhysicsGridPose FPhysicsGridRegistry::ResolveWorldPose(const FGridId Id) const
{
	if (!IsValidGrid(Id))
	{
		return FPhysicsGridPose();
	}

	// Walk to the root, collecting the chain. Gathering first and composing downward keeps each
	// offset rotated by the orientation actually above it, which is what makes a rotated ship carry
	// its interior around correctly rather than sliding it sideways.
	TArray<FGridId, TInlineAllocator<MaxDepth>> Chain;

	FGridId Current = Id;
	int32 Steps = 0;

	while (Current != InvalidGrid && Steps < MaxDepth)
	{
		Chain.Add(Current);
		Current = Grids[Current].ParentId;
		++Steps;
	}

	checkf(Steps < MaxDepth, TEXT("Physics grid chain exceeded MaxDepth; likely a cycle."));

	// Chain runs child-first, so the last entry is the root.
	const FGrid& Root = Grids[Chain.Last()];

	FSystemCoordinate Origin = Root.RootOrigin;
	FQuat Rotation = Root.LocalRotation;

	for (int32 Index = Chain.Num() - 2; Index >= 0; --Index)
	{
		const FGrid& Grid = Grids[Chain[Index]];

		// The offset is expressed in the parent's frame, so it has to be rotated by the parent's
		// accumulated orientation before it means anything in system space.
		const FVector RotatedOffset = Rotation.RotateVector(Grid.LocalOffsetCentimetres);

		Origin = FSystemCoordinate(
			Origin.Kilometres + (RotatedOffset / SpaceMMO::Coordinates::CentimetresPerKilometre));

		Rotation = Rotation * Grid.LocalRotation;
	}

	return FPhysicsGridPose(Origin, Rotation.GetNormalized());
}

FTransform FPhysicsGridRegistry::GetRenderTransform(
	const FGridId Id, const FGridId ActiveGrid) const
{
	if (!IsValidGrid(Id) || !IsValidGrid(ActiveGrid))
	{
		return FTransform::Identity;
	}

	const FPhysicsGridPose Pose = ResolveWorldPose(Id);
	const FPhysicsGridPose Active = ResolveWorldPose(ActiveGrid);

	// The separation is computed in system space at double precision and only then converted to
	// centimetres, so a grid a hundred million kilometres out still resolves to the centimetre
	// nearby. Converting first and subtracting afterwards would throw exactly that away.
	const FVector DeltaCentimetres = Pose.Origin.ToLocalCentimetres(Active.Origin);

	const FQuat InverseActive = Active.Rotation.Inverse();

	return FTransform(
		InverseActive * Pose.Rotation,
		InverseActive.RotateVector(DeltaCentimetres));
}

bool FPhysicsGridRegistry::IsWithinSimulationRange(
	const FGridId Id, const FGridId ActiveGrid) const
{
	if (!IsValidGrid(Id) || !IsValidGrid(ActiveGrid))
	{
		return false;
	}

	return ResolveWorldPose(Id).Origin.IsWithinLocalSpaceOf(ResolveWorldPose(ActiveGrid).Origin);
}
