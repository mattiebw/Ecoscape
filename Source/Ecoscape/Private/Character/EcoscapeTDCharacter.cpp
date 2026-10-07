// copyright lololol

#include "Character/EcoscapeTDCharacter.h"

#include "EcoscapeLog.h"
#include "EcoscapeStatics.h"
#include "Character/EcoscapePlayerController.h"
#include "Engine/OverlapResult.h"
#include "Kismet/KismetMathLibrary.h"
#include "World/EcoscapeTerrain.h"
#include "World/Fence/FencePlacementPreview.h"
#include "World/PlacedItem.h"
#include "World/Fence/ProceduralFenceMesh.h"

void FItemDataInterface::RerollItem()
{
	if (Type == EItemDataType::Item)
		return;
	NextItem = Folder->GetRandomItem(CurrentTerrain->TerrainName);
}

AEcoscapeTDCharacter::AEcoscapeTDCharacter()
{
	PrimaryActorTick.bCanEverTick = true;
	
	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetWorldRotation(FRotator(-80, 0, 0));
	RootComponent = Camera;
}

void AEcoscapeTDCharacter::BeginPlay()
{
	Super::BeginPlay();

	// Init paint preview
	PaintPreview = GetWorld()->SpawnActor<AActor>(PaintPreviewClass);
	PaintPreviewMesh = Cast<UStaticMeshComponent>(PaintPreview->GetComponentByClass(UStaticMeshComponent::StaticClass()));
	PaintPreview->SetActorHiddenInGame(true);
	TargetPaintRadius  = FMath::Lerp(PaintRadiusRange.X, PaintRadiusRange.Y, 0.5);
	PrevPaintRadius    = TargetPaintRadius;
	PaintSpeed = FMath::Lerp(PaintSpeedRange.X, PaintSpeedRange.Y, 0.5);
	PaintPreview->SetActorScale3D(FVector(TargetPaintRadius / PaintPreviewRadius));
	
	TargetHeight = GetActorLocation().Z;
	
	// Make fence placement preview now
	FencePlacementPreview = GetWorld()->SpawnActor<AFencePlacementPreview>();
}

void AEcoscapeTDCharacter::DoPaintTool()
{
	if (!CurrentItemData.GetItem() || !CurrentItemData.GetItem()->bIsItemPaintable)
		return;
	
	if (EcoscapePlayerController->IsAltUseDown())
	{
		// Try delete stuff
		TArray<FOverlapResult> Overlaps;
		FComponentQueryParams ComponentQueryParams;
		ComponentQueryParams.AddIgnoredActor(PaintPreview);
		GetWorld()->ComponentOverlapMulti(Overlaps, PaintPreviewMesh,
			PaintPreviewMesh->GetComponentLocation(), FRotator(0, 0, 0),
				ComponentQueryParams, FCollisionObjectQueryParams::AllObjects);

		bool bTerrainChanged = false;
		for (const auto& Overlap : Overlaps)
		{
			APlacedItem* Item = Cast<APlacedItem>(Overlap.GetActor());
			if (!Item)
				continue;

			// Only do item filtering if shift is not held
			if (!EcoscapePlayerController->IsModifierHeld())
			{
				if (CurrentItemData.Type == EItemDataType::Folder && !CurrentItemData.Folder->Items.Contains(Item->GetItemData()))
					continue;
				if (CurrentItemData.Type == EItemDataType::Item && !(CurrentItemData.Item == Item->GetItemData()))
					continue;
			}
			
			AEcoscapeTerrain* Terrain = Item->AssociatedTerrain;
			if (Terrain == EcoscapePlayerController->GetCurrentTerrain())
			{
				Terrain->PlacedItems.Remove(Item);
				auto Verts = Terrain->GetVerticiesInSphere(Item->GetActorLocation(), (Item->GetItemData()->ColourRange * Item->GetActorScale().X) + 150, true);
				for (auto& [Vertex, _] : Verts)
				{
					if (bDrawDebug)
						DrawDebugSphere(GetWorld(), Terrain->GetVertexPositionWorld(Vertex), 20, 6, FColor::Red, false, 2.5f);
					Terrain->CalculateVertColour(Vertex);
				}

				FVector Origin, Extents;
				Item->GetActorBounds(true, Origin, Extents);
				auto AffectedVertices = Terrain->GetVertexesWithinBounds(Origin, Extents, false);

				bTerrainChanged = true;
				
				Item->Destroy();
				
				for (const int Index : AffectedVertices)
					Terrain->Walkable[Index] = Terrain->IsVertWalkable(Index);
				Terrain->WalkabilityUpdated.Broadcast();
			}
		}

		if (bTerrainChanged)
			EcoscapePlayerController->GetCurrentTerrain()->FlushMesh();
		
		return;
	}
	
	if (PaintTimer > 0 || !EcoscapePlayerController->IsUseDown() || !CurrentItemData.GetItem())
		return;

	AEcoscapeTerrain* Terrain = EcoscapePlayerController->GetCurrentTerrain();
	
	// This function brute forces points in a square until one lands in a circle
	// This could be bad, and I think this maths might work better:
	// https://stackoverflow.com/a/50746409
	// For now, just use it to get a random point within the range
	const FVector2D RandomInRange = FMath::RandPointInCircle(TargetPaintRadius);
	const FVector PaintOrigin = PaintPreview->GetActorLocation();
	const FVector RayStart = FVector(RandomInRange.X + PaintOrigin.X, RandomInRange.Y + PaintOrigin.Y, Terrain->GetHighestHeight() + 50);
	const FVector RayEnd = FVector(RayStart.X, RayStart.Y, Terrain->GetLowestHeight() - 50);
	
	FHitResult Hit;
	if (GetWorld()->LineTraceSingleByChannel(Hit, RayStart, RayEnd, ECC_ITEM_PLACEABLE_ON))
	{
		if (TryPlaceItemAtHit(CurrentItemData.GetItem(), Hit))
		{
			CurrentItemData.RerollItem();
			PaintTimer = 1 / PaintSpeed;
		}
	}
}

void AEcoscapeTDCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!bIsPossessed)
		return;
	
	// Animate zoom height
	FVector Location = GetActorLocation();
	if (Location.Z != TargetHeight)
	{
		const float Diff = Location.Z - TargetHeight;
		if (FMath::Abs(Diff) < 1)
			Location.Z = TargetHeight;
		else
			Location.Z += -Diff * 10.f * GetWorld()->DeltaRealTimeSeconds;
		SetActorLocation(Location);	
	}

	// Animate paint radius
	if (PaintRadiusAlpha < 1)
	{
		PaintRadiusAlpha += DeltaSeconds * 3;
		if (PaintRadiusAlpha >= 0.99)
			PaintRadiusAlpha = 1;

		float NewPaintRadius = FMath::InterpEaseOut(PrevPaintRadius, TargetPaintRadius, PaintRadiusAlpha, 2);
		PaintPreview->SetActorScale3D(FVector(NewPaintRadius / PaintPreviewRadius));
	}

	if (PaintTimer > 0)
		PaintTimer -= DeltaSeconds;

	if (CurrentTool == ETPaintObjects)
		DoPaintTool();

	// Do tool logic
	switch (CurrentTool)
	{
	case ETNone:
		break;
	case ETPlaceObjects:
		{
			if (!CurrentItemData.GetItem())
				return;
			
			FHitResult Hit;
			if (UEcoscapeStatics::GetHitResultAtCursorByChannel(Cast<const APlayerController>(GetController()),
			                                                    FloorChannel, true, Hit, TArray<AActor*>()))
			{
				ItemPreview->UpdateWithHitInfo(Hit);
			}
		}
		break;
	case ETPaintObjects:
		{
			FHitResult Hit;
			if (UEcoscapeStatics::GetHitResultAtCursorByChannel(Cast<const APlayerController>(GetController()),
			                                                    FloorChannel, true, Hit, TArray<AActor*>()))
			{
				PaintPreview->SetActorLocation(Hit.ImpactPoint);
			}
		}
		break;
	case ETDestroyObjects:
		{
			CheckHoveredObject();
		}
		break;
	case ETPlaceFence:
		{
			if (FencePlacementStage == EFPPlacing)
			{
				// We're placing, so update the positions of the preview
				FHitResult Hit;
				if (UEcoscapeStatics::GetHitResultAtCursorByChannel(Cast<const APlayerController>(GetController()), FloorChannel, true, Hit, TArray<AActor*>()))
				{
					if (AEcoscapeTerrain* Terrain = Cast<AEcoscapeTerrain>(Hit.GetActor()))
					{
						auto XY = Terrain->GetVertexXY(Terrain->GetClosestVertex(Hit.ImpactPoint));
						auto Pos = Terrain->GetVertexPositionWorld(Terrain->GetVertexIndex(XY.X, XY.Y));
						if (bDrawDebug)
							DrawDebugSphere(GetWorld(), Pos, 20, 6, FColor::Red);
						
						FencePlacementPreview->UpdatePreview(Terrain->GetVertexXY(Terrain->GetClosestVertex(Hit.ImpactPoint)));
					}
				}
			}
		}
		break;
	default: UE_LOG(LogEcoscape, Error, TEXT("Ticking unimplemented tool: %i"), static_cast<int>(CurrentTool)); break;
	}
}

void AEcoscapeTDCharacter::SetCurrentTool(EEcoscapeTool NewTool)
{
	// Create or destroy item preview
	if (NewTool != ETPlaceObjects && ItemPreview)
	{
		ItemPreview->Destroy();
		ItemPreview = nullptr;
	} else if (NewTool == ETPlaceObjects)
		SetItemPreview(CurrentItemData.GetItem());

	if (NewTool != ETPlaceFence)
		FencePlacementPreview->DisablePreview();

	if (CurrentTool != ETPlaceFence)
		FencePlacementStage = EFPNone;

	// Show or hide paint preview depending on tool
	if (NewTool == ETPaintObjects)
		PaintPreview->SetActorHiddenInGame(false);
	else
		PaintPreview->SetActorHiddenInGame(true);
	
	CurrentTool = NewTool;
	OnToolChanged.Broadcast(NewTool);

	HighlightObject(nullptr);
}

void AEcoscapeTDCharacter::AddMovementInput(FVector WorldDirection, float ScaleValue, bool bForce)
{
	AddActorWorldOffset(WorldDirection * ScaleValue * Speed);

	const FVector Loc = GetActorLocation();
	SetActorLocation(FVector(FMath::Clamp(Loc.X, PlayRangeMin.X, PlayRangeMax.X), FMath::Clamp(Loc.Y, PlayRangeMin.Y, PlayRangeMax.Y), Loc.Z));
}

void AEcoscapeTDCharacter::OnToolUsed()
{
	switch (CurrentTool)
	{
	case ETNone:
		{
			FHitResult Hit;
			if (UEcoscapeStatics::GetHitResultAtCursorByChannel(Cast<const APlayerController>(GetController()), ECC_WorldDynamic, false, Hit, TArray<AActor*>()))
			{
				if (ABaseAnimal* Animal = Cast<ABaseAnimal>(Hit.GetActor()))
				{
					AnimalUI->Execute_SetAnimal(AnimalUI.GetObject(), Animal);
					AnimalUI->Execute_Show(AnimalUI.GetObject());
				}
			}
		}
	case ETPlaceObjects:
		{
			// Check we have an item
			if (CurrentItemData.GetItem() == nullptr)
				break;
			
			// Check position is valid
			if (!ItemPreview || !ItemPreview->IsValidPlacement())
			{
				OnFailedPlacementAttempt();
				break;
			}

			// Actually place the item
			FHitResult Hit;
			const TArray<AActor*> IgnoredActors;
			if (UEcoscapeStatics::GetHitResultAtCursorByChannel(Cast<const APlayerController>(GetController()), FloorChannel, true, Hit, IgnoredActors))
			{
				FRotator Rot = ItemPreview->GetActorRotation();
				APlacedItem* Item = APlacedItem::SpawnItem(GetWorld(), CurrentItemData.GetItem(), Hit.Location, FVector(PlacedItemScale), FRotator(Rot.Pitch, PlacedItemRotation, Rot.Roll));
				Item->AddActorWorldOffset(FVector(0, 0, UEcoscapeStatics::GetZUnderOrigin(Item) + CurrentItemData.GetItem()->ZOffset)); // Move it so the bottom of the mesh is on the ground
				OnItemPlaced(Item->GetActorLocation(), Item);

				if (AEcoscapeTerrain* Terrain = Cast<AEcoscapeTerrain>(Hit.GetActor()))
				{
					Item->AssociatedTerrain = Terrain; 
					Terrain->PlacedItems.Add(Item);
					Terrain->OnItemPlaced(Item);
					auto Verts = Terrain->GetVerticiesInSphere(Hit.ImpactPoint, CurrentItemData.GetItem()->ColourRange * PlacedItemScale, true);
					for (auto& [Vertex, _] : Verts)
					{
						if (bDrawDebug)
							DrawDebugSphere(GetWorld(), Terrain->GetVertexPositionWorld(Vertex), 20, 6, FColor::Red, false, 2.5f);
						Terrain->CalculateVertColour(Vertex);
					}
					Terrain->FlushMesh();
				}

				// Reroll item
				CurrentItemData.RerollItem();
				ItemPreview->SetItem(CurrentItemData.GetItem());
			}
		}
		break;
	case ETPaintObjects:
		{
		}
		break;
	case ETDestroyObjects:
		{
			if (APlacedItem* Item = Cast<APlacedItem>(HighlightedObject))
			{
				FVector Origin, Extents;
				Item->GetActorBounds(true, Origin, Extents);
				Item->Destroy();
				
				if (AEcoscapeTerrain* Terrain = Item->AssociatedTerrain)
				{
					Terrain->PlacedItems.Remove(Item);
					auto Verts = Terrain->GetVerticiesInSphere(Item->GetActorLocation(), (Item->GetItemData()->ColourRange * Item->GetActorScale().X) + 150, true);
					for (auto& [Vertex, _] : Verts)
					{
						if (bDrawDebug)
							DrawDebugSphere(GetWorld(), Terrain->GetVertexPositionWorld(Vertex), 20, 6, FColor::Red, false, 2.5f);
						Terrain->CalculateVertColour(Vertex);
					}
					Terrain->FlushMesh();
					
					auto AffectedVertices = Terrain->GetVertexesWithinBounds(Origin, Extents, false);
					for (const int Index : AffectedVertices)
						Terrain->Walkable[Index] = Terrain->IsVertWalkable(Index);
					Terrain->WalkabilityUpdated.Broadcast();
				}
				else
					UE_LOG(LogEcoscape, Error, TEXT("Placed item being destroyed by TDCharacter has no associated terrain!"));
				
				HighlightObject(nullptr);
			} else if (AProceduralFenceMesh* Fence = Cast<AProceduralFenceMesh>(HighlightedObject))
			{
				if (!Fence->bDestroyable)
					return;

				Fence->Destroy();
				
				if (Fence->AssociatedTerrain)
				{
					Fence->AssociatedTerrain->PlacedFences.Remove(Fence);
					auto Verts = Fence->AssociatedTerrain->GetFenceVerticies(Fence->Start, Fence->End);
					for (int Vert : Verts)
						Fence->AssociatedTerrain->Walkable[Vert] = Fence->AssociatedTerrain->IsVertWalkable(Vert);
					Fence->AssociatedTerrain->WalkabilityUpdated.Broadcast();
				}
				
				HighlightObject(nullptr);
			}
		}
		break;
	case ETPlaceFence:
		{
			if (FencePlacementStage == EFPNone)
			{
				// We haven't started placing yet.
				// Take the current position and turn that into our start
				FHitResult Hit;
				if (UEcoscapeStatics::GetHitResultAtCursorByChannel(Cast<const APlayerController>(GetController()), FloorChannel, true, Hit, TArray<AActor*>()))
				{
					if (AEcoscapeTerrain* Terrain = Cast<AEcoscapeTerrain>(Hit.GetActor()))
					{
						FencePlacementPreview->Terrain = Terrain;
						FencePlacementPreview->StartPreview(Terrain->GetVertexXY(Terrain->GetClosestVertex(Hit.ImpactPoint)));
						FencePlacementStage = EFPPlacing;
					}
				}
			}
			else
			{
				// This is our second location.
				// If this is valid, we can create our fence now

				if (FencePlacementPreview->bValid)
				{
					FencePlacementPreview->CreateFence();
					FencePlacementPreview->DisablePreview();
					FencePlacementStage = EFPNone;
				} else
				{
					OnFailedPlacementAttempt();
				}
			}
		}
		break;
	default: UE_LOG(LogEcoscape, Error, TEXT("Attempted to use unimplemented tool: %i"), static_cast<int>(CurrentTool)); break;
	}
}

void AEcoscapeTDCharacter::OnToolAltUsed()
{
	switch (CurrentTool)
	{
		case ETPlaceObjects:
			{
				if (!CurrentItemData.GetItem())
					return;
				
				PlacedItemRotation += 90;
				PlacedItemRotation = FMath::Fmod(PlacedItemRotation, 360);
				if (ItemPreview)
					ItemPreview->SetTargetRotation(PlacedItemRotation);
			}
		break;
		case ETPaintObjects:
			{
				
			}
			break;
		case ETPlaceFence:
			{
				if (FencePlacementStage == EFPPlacing)
				{
					// We're cancelling the place
					FencePlacementStage = EFPNone;
					FencePlacementPreview->DisablePreview();
					FenceStart = FVector2D::ZeroVector;
				}
			}
			break;
		default: UE_LOG(LogEcoscape, Error, TEXT("Attempted to use unimplemented tool: %i"), static_cast<int>(CurrentTool)); break;
	}
}

void AEcoscapeTDCharacter::ResetTool(bool bInstant)
{
	switch (CurrentTool)
	{
	case ETPlaceObjects:
		{
			PlacedItemRotation = 0;
			ItemPreview->SetTargetRotation(PlacedItemRotation, bInstant);
			PlacedItemScale = 1;
			ItemPreview->SetTargetScale(PlacedItemScale, bInstant);
			break;
		}
	case ETPaintObjects:
		{
			PrevPaintRadius = PaintPreview->GetActorScale().X * PaintPreviewRadius;
			TargetPaintRadius = FMath::Lerp(PaintRadiusRange.X, PaintRadiusRange.Y, 0.5);
			PaintRadiusAlpha = 0;
			PaintSpeed = FMath::Lerp(PaintSpeedRange.X, PaintSpeedRange.Y, 0.5);
		}
		break;
	default: UE_LOG(LogEcoscape, Error, TEXT("Attempted to use unimplemented tool: %i"), static_cast<int>(CurrentTool)); break;
	}
}

void AEcoscapeTDCharacter::AddScrollInput(float Value)
{
	if (Value == 0)
		return;
	
	if (CurrentTool == ETPlaceObjects && EcoscapePlayerController->IsModifierHeld())
	{
		if (!CurrentItemData.GetItem())
			return;
		PlacedItemScale = FMath::Clamp(PlacedItemScale + Value * 0.2f, CurrentItemData.GetItem()->ScaleBounds.X, CurrentItemData.GetItem()->ScaleBounds.Y);
		if (ItemPreview)
			ItemPreview->SetTargetScale(PlacedItemScale);
	}
	else if (CurrentTool == ETPaintObjects && EcoscapePlayerController->IsModifierHeld())
	{
		PrevPaintRadius = PaintPreview->GetActorScale().X * PaintPreviewRadius;
		TargetPaintRadius = FMath::Clamp(TargetPaintRadius + Value * 100, PaintRadiusRange.X, PaintRadiusRange.Y);
		PaintRadiusAlpha = 0;
	}
	else
		TargetHeight = FMath::Clamp(TargetHeight + -Value * ZoomSensitivity, HeightBounds.X, HeightBounds.Y);
}

void AEcoscapeTDCharacter::SetCurrentItem(UPlaceableItemData* Item)
{
	CurrentItemData.Type = EItemDataType::Item;
	CurrentItemData.Item = Item;

	if (Item)
		SetItemPreview(Item);
	else if (ItemPreview && !Item)
	{
		ItemPreview->Destroy();
		ItemPreview = nullptr;
	}
}

void AEcoscapeTDCharacter::SetCurrentFolder(UItemFolder* Folder)
{
	CurrentItemData.Type = EItemDataType::Folder;
	CurrentItemData.Folder = Folder;
	CurrentItemData.RerollItem();
	
	if (CurrentItemData.GetItem())
		SetItemPreview(CurrentItemData.GetItem());
	else if (ItemPreview && !CurrentItemData.GetItem())
	{
		ItemPreview->Destroy();
		ItemPreview = nullptr;
	}
}

void AEcoscapeTDCharacter::DeselectItem()
{
	CurrentItemData.Type = EItemDataType::None;
	CurrentItemData.Folder = nullptr;
	CurrentItemData.Item = nullptr;
	
	if (ItemPreview)
	{
		ItemPreview->Destroy();
		ItemPreview = nullptr;
	}
}

void AEcoscapeTDCharacter::GoToTerrain(AEcoscapeTerrain* Terrain)
{
	Terrain->GetXYBounds(PlayRangeMin, PlayRangeMax);
	PlayRangeMin.X += Terrain->ExteriorTileCount * Terrain->GetScale();
	PlayRangeMin.Y += Terrain->ExteriorTileCount * Terrain->GetScale();
	PlayRangeMax.X -= Terrain->ExteriorTileCount * Terrain->GetScale();
	PlayRangeMax.Y -= Terrain->ExteriorTileCount * Terrain->GetScale();

	if (CurrentItemData.GetItem()
		&& !CurrentItemData.GetItem()->IsValidForTerrain(Terrain))
	{
		SetCurrentItem(nullptr);
	}
	
	if (ItemPreview)
	{
		ItemPreview->Destroy();
		ItemPreview = nullptr;
	}
		//ItemPreview->CurrentTerrain = Terrain;
	
	CurrentItemData.CurrentTerrain = Terrain;
}

void AEcoscapeTDCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	// Set possessed flag
	bIsPossessed = true;
	
	// Enable mouse
	if (AEcoscapePlayerController* PlayerController = Cast<AEcoscapePlayerController>(NewController))
	{
		PlayerController->SetMouseEnabled(true);
		EcoscapePlayerController = PlayerController;
	}
	else
		UE_LOG(LogEcoscape, Error, TEXT("Ecoscape pawn possessed by non-ecoscape controller!"));
	
	// Call set tool with our current tool so that we recreate item preview or anything else we need to do
	SetCurrentTool(CurrentTool);
}

void AEcoscapeTDCharacter::UnPossessed()
{
	Super::UnPossessed();
	
	bIsPossessed = false;

	if (ItemPreview)
	{
		ItemPreview->Destroy();
		ItemPreview = nullptr;
	}
	if (FencePlacementPreview)
		FencePlacementPreview->DisablePreview();
	FencePlacementStage = EFPNone;
	PaintPreview->SetActorHiddenInGame(true);
	HighlightObject(nullptr);
}

void AEcoscapeTDCharacter::CheckHoveredObject()
{
	FHitResult Hit;
	UEcoscapeStatics::GetHitResultAtCursorByChannel(Cast<const APlayerController>(GetController()), ECC_HIGHLIGHTABLE, true, Hit, TArray<AActor*>());
	// No need to check for null here, as passing null to HighlightObject() simply unhighlights anything and sets HighlightedObject to null
	// Therefore, we have to be careful when using HighlightedObject
	AEcoscapeObject* EcoscapeObject = Cast<AEcoscapeObject>(Hit.GetActor());
	HighlightObject(EcoscapeObject);
}

void AEcoscapeTDCharacter::HighlightObject(AEcoscapeObject* Object)
{
	if (HighlightedObject && HighlightedObject->Outline)
		HighlightedObject->Outline->HideOutline();
	HighlightedObject = Object;
	if (HighlightedObject && HighlightedObject->Outline)
		HighlightedObject->Outline->ShowOutline();
}

void AEcoscapeTDCharacter::CreateItemPreview()
{
	ItemPreview = GetWorld()->SpawnActor<APlaceableItemPreview>(ItemPreviewClass, FVector(0, 0, 0), FRotator::ZeroRotator);
	ItemPreview->SetTargetRotation(PlacedItemRotation, true);
	ItemPreview->SetTargetScale(PlacedItemScale, true);
	ItemPreview->CurrentTerrain = EcoscapePlayerController->GetCurrentTerrain();
}

void AEcoscapeTDCharacter::SetItemPreview(UPlaceableItemData* ItemData)
{
	if (!ItemPreview)
		CreateItemPreview();
	ItemPreview->SetItem(ItemData);
}

bool AEcoscapeTDCharacter::TryPlaceItemAtHit(UPlaceableItemData* Item, FHitResult& Hit)
{
	// Check normal
	if (UEcoscapeStatics::AngleBetweenDirectionsDeg(FVector(0, 0, 1), Hit.ImpactNormal) > Item->MaxAngle)
		return false;

	// Check position is within playable space
	if (!EcoscapePlayerController->GetCurrentTerrain()->IsPositionWithinPlayableSpace(Hit.ImpactPoint))
		return false;

	// Check we aren't too deep underwater
	float WaterDepth = EcoscapePlayerController->GetCurrentTerrain()->GetWaterHeight() - Hit.ImpactPoint.Z;
	auto ItemData = CurrentItemData.GetItem();
	if ((ItemData->bHasMaxWaterDepth && WaterDepth > ItemData->MaxWaterDepth)
		|| (ItemData->bHasMinWaterDepth && WaterDepth < ItemData->MinWaterDepth))
	{
		return false;
	}
	
	float Yaw = FMath::RandRange(0, 360);
	
	// Spawn an object to use with the checks
	APlacedItem* PlacedItem = APlacedItem::SpawnItem(GetWorld(), Item,
													 Hit.ImpactPoint + Item->ZOffset, FVector::OneVector, FRotator(0, Yaw, 0));
	PlacedItem->AddActorLocalOffset(FVector(0, 0, UEcoscapeStatics::GetZUnderOrigin(PlacedItem)));
	
	auto Rotation = UKismetMathLibrary::Conv_VectorToRotator(Hit.ImpactNormal);
	FVector UpVector = PlacedItem->GetActorUpVector();
	FVector NormalVector = UKismetMathLibrary::VLerp(UpVector, Hit.ImpactNormal, Item->NormalAlpha);
	FVector RotationAxis = FVector::CrossProduct(UpVector, NormalVector);
	RotationAxis.Normalize();
	float DotProduct = FVector::DotProduct(UpVector, NormalVector);
	float RotationAngle = acosf(DotProduct);
	FQuat Quat = FQuat(RotationAxis, RotationAngle);
	FQuat RootQuat = PlacedItem->GetActorQuat();
	FQuat NewQuat = Quat * RootQuat;
	Rotation = NewQuat.Rotator();
	Rotation.Yaw = Yaw;
	PlacedItem->SetActorRotation(Rotation);
	
	// Get any overlapping components
	FComponentQueryParams ComponentQueryParams;
	ComponentQueryParams.AddIgnoredActor(PlacedItem);
	TArray<FOverlapResult> OverlapResults;
	GetWorld()->ComponentOverlapMulti(OverlapResults, PlacedItem->GetMesh(),
		PlacedItem->GetActorLocation(), Rotation,
		ComponentQueryParams, FCollisionObjectQueryParams::AllObjects);
	
	// Check that there are no overlaps with objects that block placement
	for (const auto OverlapResult : OverlapResults)
	{
		if (OverlapResult.Component->GetCollisionResponseToChannel(ECC_BLOCKS_ITEM_PLACEMENT) == ECR_Block)
		{
			PlacedItem->Destroy();
			return false;
		}
	}

	// Placement should be valid. Do other placement things
	OnItemPlaced(PlacedItem->GetActorLocation(), PlacedItem);
	if (AEcoscapeTerrain* Terrain = Cast<AEcoscapeTerrain>(Hit.GetActor()))
	{
		PlacedItem->AssociatedTerrain = Terrain; 
		Terrain->PlacedItems.Add(PlacedItem);
		Terrain->OnItemPlaced(PlacedItem);
		auto Verts = Terrain->GetVerticiesInSphere(Hit.ImpactPoint, CurrentItemData.GetItem()->ColourRange, true);
		for (auto& [Vertex, _] : Verts)
		{
			if (bDrawDebug)
				DrawDebugSphere(GetWorld(), Terrain->GetVertexPositionWorld(Vertex), 20, 6, FColor::Red, false, 2.5f);
			Terrain->CalculateVertColour(Vertex);
		}
		Terrain->FlushMesh();
	}

	return true;
}
