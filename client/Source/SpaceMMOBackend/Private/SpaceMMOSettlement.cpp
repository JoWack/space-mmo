#include "SpaceMMOSettlement.h"

ASpaceMMOSettlementActor::ASpaceMMOSettlementActor()
{
	PrimaryActorTick.bCanEverTick = false;

	// Movable, like everything with a system coordinate: the station carrying this moves it on every
	// render-origin rebase, and a static root would refuse.
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Movable);
	SetRootComponent(Root);
}

void ASpaceMMOSettlementActor::GetBerths(TArray<const USpaceMMOBerthComponent*>& OutBerths) const
{
	OutBerths.Reset();

	TInlineComponentArray<USpaceMMOBerthComponent*> Found(this);

	for (const USpaceMMOBerthComponent* Berth : Found)
	{
		OutBerths.Add(Berth);
	}
}

void ASpaceMMOSettlementActor::GetServicePoints(TArray<const USpaceMMOServicePointComponent*>& OutPoints) const
{
	OutPoints.Reset();

	TInlineComponentArray<USpaceMMOServicePointComponent*> Found(this);

	for (const USpaceMMOServicePointComponent* Point : Found)
	{
		OutPoints.Add(Point);
	}
}
