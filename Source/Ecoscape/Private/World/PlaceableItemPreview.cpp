// copyright lololol

#include "World/PlaceableItemPreview.h"

#include "EcoscapeLog.h"
#include "EcoscapeStatics.h"
#include "EcoscapeStats.h"
#include "Engine/OverlapResult.h"
#include "Kismet/KismetMathLibrary.h"
#include "World/EcoscapeTerrain.h"

DECLARE_CYCLE_STAT(TEXT("Move Item Preview"), STAT_MovePreview, STATGROUP_Ecoscape);

APlaceableItemPreview::APlaceableItemPreview()
{
	PrimaryActorTick.bCanEverTick = false;

	MainMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	MainMesh->SetMobility(EComponentMobility::Movable);
	MainMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
	RootComponent = MainMesh;
}

void APlaceableItemPreview::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	
	// Update rotation
	if (CurrentRotationAlpha < 1)
	{
		CurrentRotationAlpha += (1 - CurrentRotationAlpha) * 10 * DeltaSeconds;
		if (1 - CurrentRotationAlpha < 0.02)
			CurrentRotationAlpha = 1;

		const auto CurrentRotation = GetActorRotation();
		SetActorRotation(FMath::Lerp(CurrentRotation, FRotator(CurrentRotation.Pitch, TargetItemRotation, CurrentRotation.Roll), CurrentRotationAlpha));
	}

	// Update scale
	if (CurrentScaleAlpha < 1)
	{
		CurrentScaleAlpha += (1 - CurrentScaleAlpha) * 10 * DeltaSeconds;
		if (1 - CurrentScaleAlpha < 0.02)
			CurrentScaleAlpha = 1;

		const float Scale = FMath::InterpEaseOut(static_cast<float>(GetActorScale().X), TargetItemScale, CurrentScaleAlpha, 2);
		SetActorScale3D(FVector(Scale));
	}
}

void APlaceableItemPreview::SetItem(UPlaceableItemData* Item)
{
	if (!Item)
	{
		UE_LOG(LogEcoscape, Error, TEXT("In APlaceableItemPreview::SetItem, Item is null!"));
		return;
	}
	
	CurrentItem = Item;
	MainMesh->SetStaticMesh(Item->Mesh);
	UEcoscapeStatics::SetAllMaterials(MainMesh, InvalidMaterial);
}

void APlaceableItemPreview::UpdateWithHitInfo(FHitResult Hit)
{
	SCOPE_CYCLE_COUNTER(STAT_MovePreview);
	
	SetActorLocation(Hit.Location + FVector(0, 0, UEcoscapeStatics::GetZUnderOrigin(this)));

	auto CurrentRotation = GetActorRotation();
	auto Rotation = UKismetMathLibrary::Conv_VectorToRotator(Hit.ImpactNormal);
	
	SetActorRotation(FRotator(0, TargetItemRotation, 0));
	FVector UpVector = GetActorUpVector();
	FVector NormalVector = UKismetMathLibrary::VLerp(UpVector, Hit.ImpactNormal, CurrentItem->NormalAlpha);
	FVector RotationAxis = FVector::CrossProduct(UpVector, NormalVector);
	RotationAxis.Normalize();
	float DotProduct = FVector::DotProduct(UpVector, NormalVector);
	float RotationAngle = acosf(DotProduct);
	FQuat Quat = FQuat(RotationAxis, RotationAngle);
	FQuat RootQuat = RootComponent->GetComponentQuat();
	FQuat NewQuat = Quat * RootQuat;
	Rotation = NewQuat.Rotator();
	Rotation.Yaw = CurrentRotation.Yaw;
	SetActorRotation(Rotation);

	AddActorLocalOffset(FVector(0, 0, CurrentItem->ZOffset));

	// Terrain dependent checks
	if (CurrentTerrain)
	{
		// Check position is in playable area of current terrain
		if (!CurrentTerrain->IsPositionWithinPlayableSpace(Hit.ImpactPoint))
		{
			bIsValidPlacement = false;
			UEcoscapeStatics::SetAllMaterials(MainMesh, InvalidMaterial);
			return;
		}
		
		// Check we aren't too deep underwater
		float WaterDepth = CurrentTerrain->GetWaterHeight() - Hit.ImpactPoint.Z;
		if ((CurrentItem->bHasMaxWaterDepth && WaterDepth > CurrentItem->MaxWaterDepth)
			|| (CurrentItem->bHasMinWaterDepth && WaterDepth < CurrentItem->MinWaterDepth))
		{
			bIsValidPlacement = false;
			UEcoscapeStatics::SetAllMaterials(MainMesh, InvalidMaterial);
			return;
		}
	}

	// Check to see if position is valid or not
	// First, set the scale to target scale, so we check against that instead of the lerped one.
	const float PreviousScale = GetActorScale().X;
	SetActorScale3D(FVector(TargetItemScale));

	// Get any overlapping components
	FComponentQueryParams ComponentQueryParams;
	ComponentQueryParams.AddIgnoredActor(MainMesh->GetOwner());
	TArray<FOverlapResult> OverlapResults;
	GetWorld()->ComponentOverlapMulti(OverlapResults, MainMesh,
		MainMesh->GetComponentLocation(), FRotator(Rotation.Pitch, TargetItemRotation, Rotation.Roll),
		ComponentQueryParams, FCollisionObjectQueryParams::AllObjects);

	// Reset scale
	SetActorScale3D(FVector(PreviousScale));

	// Check that there are no overlaps with objects that block placement
	bIsValidPlacement = true;
	for (const auto Item : OverlapResults)
	{
		if (Item.Component->GetCollisionResponseToChannel(BlockingChannel) == ECR_Block)
		{
			bIsValidPlacement = false;
			break;
		}
	}

	// Next, check that the normal is within range
	if (UEcoscapeStatics::AngleBetweenDirectionsDeg(FVector(0, 0, 1), Hit.ImpactNormal) > CurrentItem->MaxAngle)
		bIsValidPlacement = false;
	
	// Update preview material
	if (bIsValidPlacement)
		UEcoscapeStatics::SetAllMaterials(MainMesh, ValidMaterial);
	else
		UEcoscapeStatics::SetAllMaterials(MainMesh, InvalidMaterial);
}

void APlaceableItemPreview::SetTargetRotation(const float NewValue, const bool bInstant)
{
	TargetItemRotation = NewValue;
	CurrentRotationAlpha = bInstant ? .99f : 0;
}

void APlaceableItemPreview::SetTargetScale(const float NewValue, const bool bInstant)
{
	TargetItemScale = NewValue;
	CurrentScaleAlpha = bInstant ? .99f : 0;
}
