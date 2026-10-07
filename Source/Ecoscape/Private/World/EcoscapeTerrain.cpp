// copyright lololol

#include "World/EcoscapeTerrain.h"

#include "Ecoscape.h"
#include "EcoscapeGameInstance.h"
#include "EcoscapeLog.h"
#include "EcoscapeStatics.h"
#include "EcoscapeStats.h"
#include "KismetProceduralMeshLibrary.h"
#include "NavigationSystem.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/OverlapResult.h"
#include "Kismet/KismetMathLibrary.h"
#include "Misc/FileHelper.h"
#include "Serialization/BufferArchive.h"
#include "World/FastNoise.h"
#include "World/StagedItemComponent.h"
#include "World/Fence/Fence.h"
#include "World/Fence/ProceduralFence.h"
#include "World/Fence/ProceduralFenceMesh.h"

#if WITH_EDITOR
#include "MessageLogModule.h"
#endif

DECLARE_CYCLE_STAT(TEXT("Terrain: Generate"), STAT_GenTerrain, STATGROUP_EcoscapeTerrain);
DECLARE_CYCLE_STAT(TEXT("Terrain: Get Nearest Vertex"), STAT_GetNearestVert, STATGROUP_EcoscapeTerrain);
DECLARE_CYCLE_STAT(TEXT("Terrain: Add Vertex Color"), STAT_AddVertColor, STATGROUP_EcoscapeTerrain);
DECLARE_CYCLE_STAT(TEXT("Terrain: Get Verticies In Sphere"), STAT_VertsInSphere, STATGROUP_EcoscapeTerrain);
DECLARE_CYCLE_STAT(TEXT("Terrain: Is Vert Walkable"), STAT_VertWalkableTest, STATGROUP_EcoscapeTerrain);

FORCEINLINE void operator-=(FColor& LHS, const FColor& RHS)
{
	LHS.R = (uint8) FMath::Clamp((int32) LHS.R - (int32) RHS.R, 0, 255);
	LHS.G = (uint8) FMath::Clamp((int32) LHS.G - (int32) RHS.G, 0, 255);
	LHS.B = (uint8) FMath::Clamp((int32) LHS.B - (int32) RHS.B, 0, 255);
	LHS.A = (uint8) FMath::Clamp((int32) LHS.A - (int32) RHS.A, 0, 255);
}

AEcoscapeTerrain::AEcoscapeTerrain()
{
	PrimaryActorTick.bCanEverTick = false;

	ProceduralMeshComponent = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Procedural Mesh"));
	ProceduralMeshComponent->SetupAttachment(GetRootComponent());
	ProceduralMeshComponent->SetCollisionResponseToChannel(ECC_ITEM_PLACEABLE_ON, ECR_Block);
}

void AEcoscapeTerrain::CalculateDiversity()
{
	Deadness = 0;
	Diversity = 0;
	
	if (PlacedItems.Num() == 0)
	{
		DiversityMessage = FText::FromString("Your habitat could use some items.");
		return;	
	}
	
	TMap<FString, float> Values;
	float TotalWeight = 0;

	// Add up all the diversity weights into the map
	int ItemTypes = 0;
	for (APlacedItem* Item : PlacedItems)
	{
		FString Index = Item->GetItemData()->ItemType.IsEmpty() ? Item->GetItemData()->GetName() : Item->GetItemData()->ItemType;
		if (Values.Contains(Index))
			Values[Index] += Item->GetItemData()->DiversityWeight;
		else
		{
			ItemTypes += Item->GetItemData()->ItemTypesValue;
			Values.Add(Index, Item->GetItemData()->DiversityWeight);
		}

		TotalWeight += Item->GetItemData()->DiversityWeight;
		Deadness    += Item->GetItemData()->Deadness;
	}
	
	// Item type factor
	// int ItemTypes = 0;
	// TArray<UPlaceableItemData*> ItemTypesArray;
	// Values.GetKeys(ItemTypesArray);
	// TArray<FString> SeenItemTypeStrings;
	// // TODO: Use some custom int-based data type instead of strings to prevent a bunch of unnessesary allocs
	// for (const UPlaceableItemData* Data : ItemTypesArray)
	// {
	// 	if (Data->ItemType.IsEmpty())
	// 		ItemTypes++;
	// 	else if (!SeenItemTypeStrings.Contains(Data->ItemType))
	// 	{
	// 		SeenItemTypeStrings.Add(Data->ItemType);
	// 		ItemTypes++;
	// 	}
	// }
	const double ItemTypeFactor = FMath::Clamp(0.1 + (static_cast<double>(ItemTypes) / 5 * 0.9), 0.1, 1); // From 0.5 to 1, depends on how many types of items we have

	float WeightMean = 0;
	for (const auto& Item : Values)
		WeightMean += Item.Value;
	WeightMean /= Values.Num();

	double DifferencesMean = 0, TotalDifference = 0;
	for (const auto& Item : Values)
	{
		float Diff = WeightMean - Item.Value;
		if (Diff > 0) // If there's less than something than average, don't effect the values as much.
			Diff *= 0.1f;
		DifferencesMean += abs(Diff);
		TotalDifference += abs(Diff);
	}
	DifferencesMean /= Values.Num();

	double WeightFactor = 0;
		//WeightFactor = FMath::Exp((DifferencesMean / WeightMean) * -1.5);
	if (WeightMean != 0)
		WeightFactor = 1 + (FMath::Pow((TotalDifference / TotalWeight), 3) * -1.5);
	// WeightFactor = FMath::Clamp(WeightFactor, 0.1f, 1);
	
	// float StandardDeviation = 0, Sum = 0;
	// for (const UPlaceableItemData* Item : ItemTypesArray)
	// {
	// 	float Diff = Values[Item] - WeightMean;
	// 	Sum += Diff * Diff;
	// }
	// StandardDeviation = FMath::Sqrt(Sum / ItemTypesArray.Num());
	//
	// const float CoefficientOfVariation = StandardDeviation / WeightMean;
	// float WeightFactor = 1 - FMath::Pow(CoefficientOfVariation, -4);
	// WeightFactor = FMath::Clamp(WeightFactor, 0, 1);
	
	double TotalWeightFactor = FMath::Clamp(TotalWeight / 250, 0.5f, 1.f);

	double DeadnessFactor = FMath::Clamp(1 - Deadness / 10, 0, 1);
	
	Diversity = ItemTypeFactor * WeightFactor * TotalWeightFactor * DeadnessFactor;

	if (Diversity < 0.8)
	{
		TArray<double> AllValues = { ItemTypeFactor, WeightFactor, TotalWeightFactor, DeadnessFactor };
		int MinIndex = 0;
		FMath::Min(AllValues, &MinIndex);
		
		if (MinIndex == 0)
			DiversityMessage = FText::FromString(FString::Printf(TEXT("Your habitat could use some more diversity in it's item types.")));
		else if (MinIndex == 1)
			DiversityMessage = FText::FromString(FString::Printf(TEXT("Your habitat has too much of one thing!")));
		else if (MinIndex == 2)
			DiversityMessage = FText::FromString(FString::Printf(TEXT("Your habitat could use some more stuff to keep the animals happy.")));
		else if (MinIndex == 3)
			DiversityMessage = FText::FromString(FString::Printf(TEXT("Your habitat is full of dead stuff!")));
	} else
	{
		DiversityMessage = FText::FromString(FString::Printf(TEXT("Your animals love your %s!"), *TerrainName));
	}
	
	// UE_LOG(LogEcoscape, Log, TEXT("%s: Diversity is %f (ITF: %f, WF: %f)"), *TerrainName, Diversity, ItemTypeFactor, WeightFactor);
}

void AEcoscapeTerrain::BeginPlay()
{
	Super::BeginPlay();

	GIIndex = UEcoscapeGameInstance::GetEcoscapeGameInstance(GetWorld())->AddTerrain(this);

	FTimerHandle Handle;
	FTimerDelegate Delegate;
	Delegate.BindLambda([this] { CalculateDiversity(); });
	GetWorldTimerManager().SetTimer(Handle, Delegate, 5, true, 0);

	Regenerate();
}

void AEcoscapeTerrain::Tick(float DeltaSeconds)
{
}

void AEcoscapeTerrain::GetPlayablePoints(TArray<FVector>& OutPoints)
{
	for (int x = ExteriorTileCount + 2; x < Width - (ExteriorTileCount + 1); x++)
		for (int y = ExteriorTileCount + 2; y < Height - (ExteriorTileCount + 1); y++)
			OutPoints.Add(GetVertexPositionWorld(GetVertexIndex(x, y)));
}

bool AEcoscapeTerrain::GetRandomDrinkLocation(FDrinkLocation& OutDrinkLocation)
{
	if (!Water || DrinkLocations.IsEmpty())
		return false;
	OutDrinkLocation = DrinkLocations[FMath::RandRange(0, DrinkLocations.Num() - 1)];
	return true;
}

bool AEcoscapeTerrain::GetClosestDrinkLocation(FVector Origin, FDrinkLocation& OutDrinkLocation)
{
	if (!Water || DrinkLocations.IsEmpty())
		return false;
	
	int ClosestIndex = 0;
	float ClosestDist = MAX_FLT;

	for (int i = 0; i < DrinkLocations.Num(); i++)
	{
		const float Dist = FVector::DistSquared(Origin, DrinkLocations[i].Location);
		if (Dist < ClosestDist)
		{
			ClosestIndex = i;
			ClosestDist = Dist;
		}
	}

	OutDrinkLocation = DrinkLocations[ClosestIndex];
	
	return true;
}

bool AEcoscapeTerrain::IsVertWalkable(int Index, bool bDiscountWet)
{
	SCOPE_CYCLE_COUNTER(STAT_VertWalkableTest);
	
	// Check range
	if (Index < 0 || Index >= Verticies.Num())
		return false;

	FVector2D Pos = GetVertexXY(Index);
	if (Pos.X < ExteriorTileCount + 2 || Pos.X > Width - ExteriorTileCount + 2
		|| Pos.Y < ExteriorTileCount + 2 || Pos.Y > Height - ExteriorTileCount + 2)
	{
		return false;
	}

	// Check water height
	if (bDiscountWet && Water && Verticies[Index].Z < Water->GetActorLocation().Z)
		return false;

	// Check collisions
	// TODO: heavy
	static FCollisionShape Box = FCollisionShape::MakeBox(FVector(Scale / 2, Scale / 2, Scale * 2));
	if (GetWorld()->OverlapBlockingTestByChannel(GetActorLocation() + Verticies[Index], FQuat::Identity, ECC_BLOCKS_HABITAT, Box))
		return false;
	
	return true;
}

TArray<int> AEcoscapeTerrain::GetVertexesWithinBounds(FVector Origin, FVector Extents, bool bIgnoreZ)
{
	TArray<int> Vertices;

	Origin -= GetActorLocation();
	// HACK - to prevent some verticies not getting checked
	Extents += FVector(50, 50, 50);

	int StartX = floor(Origin.X - Extents.X) + Scale - 1;
	StartX -= FMath::Fmod(StartX, Scale);
	int StartY = floor(Origin.Y - Extents.Y) + Scale - 1;
	StartY -= FMath::Fmod(StartY, Scale);
	
	for (float X = StartX; X <= Origin.X + Extents.X; X += Scale)
	{
		for (float Y = StartY; Y <= Origin.Y + Extents.Y; Y += Scale)
		{
			int Index = GetVertexIndex(X / Scale, Y / Scale);
			if (!bIgnoreZ)
			{
				const float Z = GetVertexPositionWorld(Index).Z;
				if (Z < Origin.Z - Extents.Z || Z > Origin.Z + Extents.Z)
					continue;
			}
			Vertices.Add(Index);
		}
	}
	
	return Vertices;
}

void AEcoscapeTerrain::OnItemPlaced(APlacedItem* NewItem)
{
	FVector Origin, Extents;
	NewItem->GetActorBounds(true, Origin, Extents);
	auto AffectedVertices = GetVertexesWithinBounds(Origin, Extents, false);
	for (const int Index : AffectedVertices)
		Walkable[Index] = IsVertWalkable(Index);
	WalkabilityUpdated.Broadcast();
}

TArray<int> AEcoscapeTerrain::GetFenceVerticies(FVector2D Start, FVector2D End)
{
	const FVector StartLoc = GetVertexPositionWorld(GetVertexIndex(Start.X, Start.Y));
	const FVector EndLoc = GetVertexPositionWorld(GetVertexIndex(End.X, End.Y));
	const float LowX = StartLoc.X < EndLoc.X ? StartLoc.X : EndLoc.X;
	const float HighX = StartLoc.X > EndLoc.X ? StartLoc.X : EndLoc.X;
	const float LowY = StartLoc.Y < EndLoc.Y ? StartLoc.Y : EndLoc.Y;
	const float HighY = StartLoc.Y > EndLoc.Y ? StartLoc.Y : EndLoc.Y;

	FCollisionShape CollisionShape;

	TArray<int> AffectedVertices;
	
	CollisionShape.SetBox(FVector3f((HighX - LowX) / 2, 30, 5000));
	AffectedVertices.Append(GetVertexesWithinBounds(FVector(LowX + (HighX - LowX) / 2, LowY, 0), CollisionShape.GetExtent(), false));
	AffectedVertices.Append(GetVertexesWithinBounds(FVector(LowX + (HighX - LowX) / 2, HighY, 0), CollisionShape.GetExtent(), false));
	CollisionShape.SetBox(FVector3f(30, (HighY - LowY) / 2, 5000));
	AffectedVertices.Append(GetVertexesWithinBounds(FVector(LowX, LowY + (HighY - LowY) / 2, 0), CollisionShape.GetExtent(), false));
	AffectedVertices.Append(GetVertexesWithinBounds(FVector(HighX, LowY + (HighY - LowY) / 2, 0), CollisionShape.GetExtent(), false));

	return AffectedVertices;
}

void AEcoscapeTerrain::OnFencePlaced(FVector2D Start, FVector2D End)
{
	TArray<int> AffectedVertices = GetFenceVerticies(Start, End);
	for (const int Vertex : AffectedVertices)
		Walkable[Vertex] = IsVertWalkable(Vertex);
	WalkabilityUpdated.Broadcast();
}

void AEcoscapeTerrain::SerialiseTerrain(FArchive& Archive)
{
	// Save/load mesh information
	Archive << Verticies;
	Archive << Triangles;
	Archive << UV0;
	Archive << VertexColors;
	Archive << Normals;
	Archive << Tangents;
	Archive << ColorOffsets;

	// Save/load placed items
	int Count = PlacedItems.Num();
	Archive << Count;

	if (Archive.IsSaving())
	{
		for (APlacedItem* Item : PlacedItems)
		{
			FTransform Transform = Item->GetActorTransform();
			Archive << Transform;
			FString ItemName = Item->GetItemData()->GetName();
			Archive << ItemName;

			UStagedItemComponent* Staged = Cast<UStagedItemComponent>(Item->GetComponentByClass(UStagedItemComponent::StaticClass()));
			bool HasStagedComponent = Staged != nullptr;
			Archive << HasStagedComponent;
			if (HasStagedComponent)
			{
				Archive << Staged->CurrentStage;
				Archive << Staged->GrowthTimer;
			}
		}
	} else
	{
		// Destroy all placed items and empty array
		for (APlacedItem* Item : PlacedItems)
			Item->Destroy();
		PlacedItems.Empty();

		for (AProceduralFenceMesh* Fence : PlacedFences)
			Fence->Destroy();
		PlacedFences.Empty();

		for (const auto& Detail : DetailActors)
			Detail.Actor->Destroy();
		DetailActors.Empty();

		PlacedItems.Reserve(Count);

		UEcoscapeGameInstance* GameInstance = UEcoscapeGameInstance::GetEcoscapeGameInstance(GetWorld());
		for (int i = 0; i < Count; i++)
		{
			// Get serialised values
			FTransform Transform;
			FString    Item;
			bool       HasStagedComp;
			Archive << Transform;
			Archive << Item;
			Archive << HasStagedComp;

			if (!GameInstance->ItemTypes.Contains(Item))
			{
				UE_LOG(LogEcoscape, Error, TEXT("Non-existent item type %s found in terrain save!"), *Item);
				continue;
			}
			UPlaceableItemData* ItemData = GameInstance->ItemTypes[Item];
			
			APlacedItem* NewItem = GetWorld()->SpawnActor<APlacedItem>(ItemData->PlacedItemClass, Transform);
			NewItem->SetItemData(ItemData);
			NewItem->AssociatedTerrain = this; 
			PlacedItems.Add(NewItem);

			if (HasStagedComp)
			{
				int   CurrentStage;
				float GrowthTimer;
				Archive << CurrentStage;
				Archive << GrowthTimer;

				NewItem->AddStagedGrowthComponent()->SetStage(CurrentStage, GrowthTimer);
			}
		}
	}

	int FenceCount = PlacedFences.Num();
	Archive << FenceCount;
	if (Archive.IsSaving())
	{
		for (AProceduralFenceMesh* Fence : PlacedFences)
		{
			FVector Loc = Fence->GetActorLocation();
			Archive << Loc;
			Fence->SerialiseFence(Archive);
		}
	} else
	{
		for (int i = 0; i < FenceCount; i++)
		{
			FVector Loc;
			Archive << Loc;
			AProceduralFenceMesh* Fence = GetWorld()->SpawnActor<AProceduralFenceMesh>(FenceClass, Loc, FRotator::ZeroRotator);
			Fence->SerialiseFence(Archive);
			PlacedFences.Add(Fence);
		}
	}

	int DetailActorCount = DetailActors.Num();
	Archive << DetailActorCount;
	if (Archive.IsSaving())
	{
		for (auto& DetailActor : DetailActors)
			Archive << DetailActor;
	} else
	{
		DetailActors.Reserve(Count);
		for (int i = 0; i < Count; i++)
		{
			FTerrainDetailActor DetailInfo;
			Archive << DetailInfo;
			const UPlaceableItemData* Item = UEcoscapeGameInstance::GetEcoscapeGameInstance(GetWorld())->ItemTypes[DetailInfo.ItemName];
			AStaticMeshActor* MeshActor = GetWorld()->SpawnActor<AStaticMeshActor>(DetailInfo.Pos, FRotator::ZeroRotator);
			MeshActor->SetMobility(EComponentMobility::Movable);
			MeshActor->GetStaticMeshComponent()->SetStaticMesh(Item->Mesh);
#if WITH_EDITOR
			MeshActor->SetFolderPath(FName(FString::Printf(TEXT("ExteriorDetail/%s"), *TerrainName)));
#endif
			MeshActor->SetMobility(EComponentMobility::Static);
			DetailInfo.Actor = MeshActor;
			DetailActors.Add(DetailInfo);
		}
	}

	Archive << DrinkLocations;

	InitWalkable();
}

bool AEcoscapeTerrain::SerialiseTerrainToFile(FString Filename)
{
	// Serialise with buffer archive
	FBufferArchive BinarySaveArchive;
	SerialiseTerrain(BinarySaveArchive);

	// Save to disk
	const FString Path = *FString::Printf(TEXT("%lsSaves/%s"), *FPaths::ProjectSavedDir(), *Filename);
	const bool SaveResult = FFileHelper::SaveArrayToFile(BinarySaveArchive, *Path);
	if (!SaveResult)
	{
		UE_LOG(LogEcoscape, Error, TEXT("Failed to save terrain to %s!"), *Path);
		return false;
	}
	
	// Empty buffer
	BinarySaveArchive.FlushCache();
	BinarySaveArchive.Empty(); 
	return true;
}

bool AEcoscapeTerrain::DeserialiseTerrainFromFile(FString Filename)
{
	TArray<uint8> BinaryArray;

	// Load and verify file
	const FString Path = *FString::Printf(TEXT("%lsSaves/%ls"), *FPaths::ProjectSavedDir(), *Filename);
	const bool LoadFileResult = FFileHelper::LoadFileToArray(BinaryArray, *Path);
	if (!LoadFileResult)
	{
		UE_LOG(LogEcoscape, Error, TEXT("In Ecoscape Terrain loader, failed to open file %s!"), *Path);
		return false;
	}
	if (BinaryArray.Num() <= 0)
	{
		UE_LOG(LogEcoscape, Error, TEXT("In Ecoscape Terrain loader, file %s is empty!"), *Path);
		return false;
	}

	// Create binary loader
	FMemoryReader BinaryLoader = FMemoryReader(BinaryArray, true);
	BinaryLoader.Seek(0);
	
	// Do the loading
	ResetMeshData();
	SerialiseTerrain(BinaryLoader);
	GenerateFence();

	// Recreate terrain from loaded data
	CreateMesh();

	// Empty buffer
	BinaryLoader.FlushCache();
	BinaryArray.Empty();
	BinaryLoader.Close();

	return true;
}

void AEcoscapeTerrain::ResetMeshData()
{	
	Verticies.Empty();
	Triangles.Empty();
	UV0.Empty();
	Normals.Empty();
	Tangents.Empty();
	VertexColors.Empty();
	ColorOffsets.Empty();
}

void AEcoscapeTerrain::GenerateVerticies()
{
	HighestHeight = -9999999;
	LowestHeight  =  9999999;
	
	static FastNoiseLite Noise; // Declare noise here so we don't have to include FastNoise.h in the header file

	Heights.Init(0, ((Width - ExteriorTileCount * 2) + 1) * ((Height - ExteriorTileCount * 2) + 1));
	
	int VertCount = 0, PlayableVertCount = 0;
	for (int x = 0; x <= Width; x++)
	{
		for (int y = 0; y <= Height; y++)
		{
			VertCount++;
			// TODO: ensure that it's never an integer
			float Z = 0;
			for (const auto& [NoiseType, CoordinateScale, HeightScale, Seed, Offset] : NoiseLayers)
			{
				switch (NoiseType)
				{
				case ENTPerlin:
					Noise.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
					break;
				case ENTSimplex:
					Noise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
					break;
				default:
					UE_LOG(LogEcoscape, Error, TEXT("Invalid noise type: %i"), static_cast<int>(NoiseType));
					break;
				}

				Noise.SetSeed(Seed);

				Z += Noise.GetNoise(x * CoordinateScale, y * CoordinateScale) * HeightScale;
			}

			// Add exterior height offset
			const int Distance = FMath::Min(FMath::Min(x, y), FMath::Min(Width - x, Height - y));
			const float Alpha = 1 - (static_cast<float>(Distance - 3) / static_cast<float>(ExteriorTileCount - 3));
			Z += FMath::Lerp(0, ExteriorHeightOffset, FMath::Clamp(Alpha, 0, 1));

			AverageHeight += Z;
			HighestHeight = FMath::Max(HighestHeight, Z);
			LowestHeight = FMath::Min(LowestHeight, Z);

			if (x >= ExteriorTileCount && x <= Width - ExteriorTileCount
				&& y >= ExteriorTileCount && y <= Height - ExteriorTileCount)
			{
				Heights[PlayableVertCount] = Z;
				PlayableVertCount++;
			}
			
			Verticies.Add(FVector(x * Scale, y * Scale, Z));
			UV0.Add(FVector2D(x * UVScale, y * UVScale));
			
			Noise.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
			Noise.SetSeed(ColorOffsetSeed);
			
			float Value = Noise.GetNoise((x + 0.1f) * 5.0f, (y + 0.1f) * 5.0f);
			Value = (Value + 1) / 2;
			Value = FMath::Lerp(ColorOffsetRange.X, ColorOffsetRange.Y, Value);
			FVector Offset = FVector(Value);
			ColorOffsets.Add(Offset);
			
			FColor VertexColor = UKismetMathLibrary::LinearColorLerp(FloorColour, ExteriorColour, FMath::Clamp(Alpha * 7, 0, 1)).ToFColor(false);
			VertexColor = UEcoscapeStatics::AddToColor(VertexColor, Offset);
			VertexColors.Add(VertexColor);
		}
	}

	AverageHeight /= VertCount;
	Heights.Sort();
}

void AEcoscapeTerrain::GenerateIndicies()
{
	int Vertex = 0;

	for (int X = 0; X < Width; ++X)
	{
		for (int Y = 0; Y < Height; ++Y)
		{
			Triangles.Add(Vertex); // Bottom left corner
			Triangles.Add(Vertex + 1); // Bottom right corner
			Triangles.Add(Vertex + Height + 1); // Top left corner
			Triangles.Add(Vertex + 1); // Bottom right corner
			Triangles.Add(Vertex + Height + 2); // Top right corner
			Triangles.Add(Vertex + Height + 1); // Top left corner

			++Vertex;
		}
		++Vertex;
	}
}

void AEcoscapeTerrain::GenerateNormals()
{
	Normals.Init(FVector::ZeroVector, Verticies.Num());

	for (int i = 0; i <  Triangles.Num(); i += 3)
	{
		const FVector P = FVector::CrossProduct(Verticies[Triangles[i + 1]] - Verticies[Triangles[i]], Verticies[Triangles[i + 2]] - Verticies[Triangles[i]]);
		Normals[Triangles[i]] += P;
		Normals[Triangles[i + 1]] += P;
		Normals[Triangles[i + 2]] += P;
	}

	for (int i = 0; i < Normals.Num(); i++)
	{
		Normals[i].Normalize();
		Normals[i] = -Normals[i];
	}
}

void AEcoscapeTerrain::GenerateFence()
{
	// This fence impl stuff is just for demonstration purposes.
#if WITH_EDITOR
	switch (FenceImplementation)
	{
	case EFIGeoScript:
		{
			int X = 3, Y = 3;
			AProceduralFence* Fence = GetWorld()->SpawnActor<AProceduralFence>(GetVertexPositionWorld(GetVertexIndex(X, Y)), FRotator::ZeroRotator);
			Fence->SplineComponent->ClearSplinePoints();
			Fence->SplineComponent->AddSplineWorldPoint(GetVertexPositionWorld(GetVertexIndex(X, Y)) + FVector(0, 0, 30));

			X += 2;
			for (; X < Width - 3; X += 2)
				Fence->SplineComponent->AddSplineWorldPoint(GetVertexPositionWorld(GetVertexIndex(X, Y)) + FVector(0, 0, 30));
			for (; Y < Height - 3; Y += 2)
				Fence->SplineComponent->AddSplineWorldPoint(GetVertexPositionWorld(GetVertexIndex(X, Y)) + FVector(0, 0, 30));
			for (; X > 3; X -= 2)
				Fence->SplineComponent->AddSplineWorldPoint(GetVertexPositionWorld(GetVertexIndex(X, Y)) + FVector(0, 0, 30));
			for (; Y > 3; Y -= 2)
				Fence->SplineComponent->AddSplineWorldPoint(GetVertexPositionWorld(GetVertexIndex(X, Y)) + FVector(0, 0, 30));

			X = 3;
			Y = 3;
			Fence->SplineComponent->AddSplineWorldPoint(GetVertexPositionWorld(GetVertexIndex(X, Y)) + FVector(0, 0, 30));	
	
			Fence->Generate();
		}
		break;
	case EFISplineMesh:
		{
			int X = 3, Y = 3;
			AFence* Fence = GetWorld()->SpawnActor<AFence>(GetVertexPositionWorld(GetVertexIndex(X, Y)), FRotator::ZeroRotator);
			Fence->Spline->ClearSplinePoints();
			Fence->Spline->AddSplineWorldPoint(GetVertexPositionWorld(GetVertexIndex(X, Y)) + FVector(0, 0, 30));

			X += 2;
			for (; X < Width - 3; X += 2)
				Fence->Spline->AddSplineWorldPoint(GetVertexPositionWorld(GetVertexIndex(X, Y)) + FVector(0, 0, 30));
			for (; Y < Height - 3; Y += 2)
				Fence->Spline->AddSplineWorldPoint(GetVertexPositionWorld(GetVertexIndex(X, Y)) + FVector(0, 0, 30));
			for (; X > 3; X -= 2)
				Fence->Spline->AddSplineWorldPoint(GetVertexPositionWorld(GetVertexIndex(X, Y)) + FVector(0, 0, 30));
			for (; Y > 3; Y -= 2)
				Fence->Spline->AddSplineWorldPoint(GetVertexPositionWorld(GetVertexIndex(X, Y)) + FVector(0, 0, 30));

			X = 3;
			Y = 3;
			Fence->Spline->AddSplineWorldPoint(GetVertexPositionWorld(GetVertexIndex(X, Y)) + FVector(0, 0, 30));	
	
			Fence->GenerateMesh();
		}
		break;
	case EFIProc:	
		{
#endif
			if (FenceMesh)
				FenceMesh->Destroy();
	
			int X = ExteriorTileCount + 1, Y = ExteriorTileCount + 1;
			FenceMesh = GetWorld()->SpawnActor<AProceduralFenceMesh>(FenceClass, GetVertexPositionWorld(GetVertexIndex(X, Y)), FRotator::ZeroRotator);
			FenceMesh->AssociatedTerrain = this;
			FenceMesh->bDestroyable = false;
			FenceMesh->Outline->DestroyComponent();
			FenceMesh->BottomSplineComponent->ClearSplinePoints();
			FenceMesh->BottomSplineComponent->AddSplineWorldPoint(GetVertexPositionWorld(GetVertexIndex(X, Y)) + FVector(0, 0, 30));
			FenceMesh->TopSplineComponent->ClearSplinePoints();
			FenceMesh->TopSplineComponent->AddSplineWorldPoint(GetVertexPositionWorld(GetVertexIndex(X, Y)) + FVector(0, 0, 250));

			X += 2;
			for (; X < Width - ExteriorTileCount - 1; X += 2)
			{
				FVector Position = GetVertexPositionWorld(GetVertexIndex(X, Y)) + FVector(0, 0, 30);
				FenceMesh->BottomSplineComponent->AddSplineWorldPoint(Position);
				FVector TopPosition = Position + FVector(0, 0, 250);
				if (bEnableWater)
					TopPosition.Z = FMath::Max(TopPosition.Z, Water->GetActorLocation().Z + 250);
				FenceMesh->TopSplineComponent->AddSplineWorldPoint(TopPosition);
			}
			for (; Y < Height - ExteriorTileCount - 1; Y += 2)
			{
				FVector Position = GetVertexPositionWorld(GetVertexIndex(X, Y)) + FVector(0, 0, 30);
				FenceMesh->BottomSplineComponent->AddSplineWorldPoint(Position);
				FVector TopPosition = Position + FVector(0, 0, 250);
				if (bEnableWater)
					TopPosition.Z = FMath::Max(TopPosition.Z, Water->GetActorLocation().Z + 250);
				FenceMesh->TopSplineComponent->AddSplineWorldPoint(TopPosition);
			}
			for (; X > ExteriorTileCount + 1; X -= 2)
			{
				FVector Position = GetVertexPositionWorld(GetVertexIndex(X, Y)) + FVector(0, 0, 30);
				FenceMesh->BottomSplineComponent->AddSplineWorldPoint(Position);
				FVector TopPosition = Position + FVector(0, 0, 250);
				if (bEnableWater)
					TopPosition.Z = FMath::Max(TopPosition.Z, Water->GetActorLocation().Z + 250);
				FenceMesh->TopSplineComponent->AddSplineWorldPoint(TopPosition);
			}
			for (; Y > ExteriorTileCount + 1; Y -= 2)
			{
				FVector Position = GetVertexPositionWorld(GetVertexIndex(X, Y)) + FVector(0, 0, 30);
				FenceMesh->BottomSplineComponent->AddSplineWorldPoint(Position);
				FVector TopPosition = Position + FVector(0, 0, 250);
				if (bEnableWater)
					TopPosition.Z = FMath::Max(TopPosition.Z, Water->GetActorLocation().Z + 250);
				FenceMesh->TopSplineComponent->AddSplineWorldPoint(TopPosition);
			}

			X = ExteriorTileCount + 1;
			Y = ExteriorTileCount + 1;
			FenceMesh->BottomSplineComponent->AddSplineWorldPoint(GetVertexPositionWorld(GetVertexIndex(X, Y)) + FVector(0, 0, 30));	
			FenceMesh->TopSplineComponent->AddSplineWorldPoint(GetVertexPositionWorld(GetVertexIndex(X, Y)) + FVector(0, 0, 250));	
	
			FenceMesh->Regenerate();
#if WITH_EDITOR
		}
		break;
	default: break;
	}
#endif
}

void AEcoscapeTerrain::CreateNavVolume()
{
	// FVector Center = GetCenterPosition();
	// ANavMeshBoundsVolume* BoundsVolume = GetWorld()->SpawnActor<ANavMeshBoundsVolume>(Center, FRotator::ZeroRotator);
	// UCubeBuilder* Builder = Cast<UCubeBuilder>(BoundsVolume->BrushBuilder);
	// Builder->X = (Width - (ExteriorTileCount * 2)) * Scale;
	// Builder->Y = (Height - (ExteriorTileCount * 2)) * Scale;
	// Builder->Z = 5000;
	// Builder->Build(GetWorld());
	// BoundsVolume->RebuildNavigationData();
	// NavMeshVolume = BoundsVolume;
}

void AEcoscapeTerrain::GenerateDrinkLocations()
{
	if (!Water)
		return;

	DrinkLocations.Empty();
	
	const float WaterZ = Water->GetActorLocation().Z;
	bool PreviousInWater = false;
	FVector PreviousLocation;
	bool First = true;
	
	for (int x = ExteriorTileCount + 2; x < Width - (ExteriorTileCount + 1); x++)
	{
		for (int y = ExteriorTileCount + 2; y < Height - (ExteriorTileCount + 1); y++)
		{
			const FVector Pos = GetVertexPositionWorld(GetVertexIndex(x, y));
			const bool PointIsInWater = Pos.Z < WaterZ;
			
			if (!First && PointIsInWater && !PreviousInWater)
			{
				// The previous is a drink location
				// Orientation is towards current
				FDrinkLocation DrinkLocation;
				DrinkLocation.Location = PreviousLocation;
				DrinkLocation.DrinkerOrientation = (Pos - PreviousLocation).GetSafeNormal();
				DrinkLocations.Add(DrinkLocation);
			} else if (!First && !PointIsInWater && PreviousInWater)
			{
				// The current is a drink location
				// Orientation is towards previous
				FDrinkLocation DrinkLocation;
				DrinkLocation.Location = Pos;
				DrinkLocation.DrinkerOrientation = (PreviousLocation - Pos).GetSafeNormal();
				DrinkLocations.Add(DrinkLocation);
			}

			PreviousInWater = PointIsInWater;
			PreviousLocation = Pos; 
			First = false;
		}
		First = true;
	}

	// TODO: A lot of repeated code here...
	First = true;
	for (int y = ExteriorTileCount + 2; y < Height - (ExteriorTileCount + 1); y++)
	{
		for (int x = ExteriorTileCount + 2; x < Width - (ExteriorTileCount + 1); x++)
		{
			const FVector Pos = GetVertexPositionWorld(GetVertexIndex(x, y));
			const bool PointIsInWater = Pos.Z < WaterZ;
			
			if (!First && PointIsInWater && !PreviousInWater)
			{
				FDrinkLocation DrinkLocation;
				DrinkLocation.Location = PreviousLocation;
				DrinkLocation.DrinkerOrientation = (Pos - PreviousLocation).GetSafeNormal();
				DrinkLocations.Add(DrinkLocation);
			} else if (!First && !PointIsInWater && PreviousInWater)
			{
				FDrinkLocation DrinkLocation;
				DrinkLocation.Location = Pos;
				DrinkLocation.DrinkerOrientation = (PreviousLocation - Pos).GetSafeNormal();
				DrinkLocations.Add(DrinkLocation);
			}

			PreviousInWater = PointIsInWater;
			PreviousLocation = Pos; 
			First = false;
		}
		First = true;
	}
}

// TODO: This function SUCKS
// Look into converting to using instanced static meshes: https://docs.unrealengine.com/4.27/en-US/BlueprintAPI/Components/InstancedStaticMesh/
void AEcoscapeTerrain::GenerateExteriorDetail()
{
	if (UEcoscapeStatics::InEditor())
		return;
	
	FVector TerrainLoc = GetActorLocation();
	
	for (int i = 0; i < ExteriorItemCount; i++)
	{
		float X, Y;

		if (FMath::FRand() < 0.5)
		{
			// X is long side
			X = FMath::RandRange(static_cast<float>(0), Width * Scale);
			Y = FMath::RandRange(static_cast<float>(0), ExteriorTileCount * Scale);
			if (FMath::FRand() < 0.5)
				Y = (Height * Scale) - Y; 
		} else
		{
			// Y is long side
			Y = FMath::RandRange(static_cast<float>(0), Height * Scale);
			X = FMath::RandRange(static_cast<float>(0), ExteriorTileCount * Scale);
			if (FMath::FRand() < 0.5)
				X = (Width * Scale) - X;
		}

		X += TerrainLoc.X;
		Y += TerrainLoc.Y;

		FHitResult Hit;
		const FVector RayStart = FVector(X, Y, GetHighestHeight() + 50);
		const FVector RayEnd = FVector(X, Y, GetLowestHeight() - 50);
		if (GetWorld()->LineTraceSingleByChannel(Hit, RayStart, RayEnd, ECC_ITEM_PLACEABLE_ON))
		{
			UPlaceableItemData* Item = ExteriorItems[FMath::RandRange(0, ExteriorItems.Num() - 1)];
			AStaticMeshActor* Mesh = GetWorld()->SpawnActor<AStaticMeshActor>(Hit.ImpactPoint, FRotator::ZeroRotator);
			Mesh->SetMobility(EComponentMobility::Movable);
			Mesh->GetStaticMeshComponent()->SetStaticMesh(Item->Mesh);
			Mesh->AddActorLocalOffset(FVector(0, 0, UEcoscapeStatics::GetZUnderOrigin(Mesh) + Item->ZOffset));
#if WITH_EDITOR
			Mesh->SetFolderPath(FName(FString::Printf(TEXT("ExteriorDetail/%s"), *TerrainName)));
#endif

			if (bDoRotationForExteriorItems)
			{
				auto Rotation = UKismetMathLibrary::Conv_VectorToRotator(Hit.ImpactNormal);
				FVector UpVector = Mesh->GetActorUpVector();
				FVector NormalVector = UKismetMathLibrary::VLerp(UpVector, Hit.ImpactNormal, 0.4f);
				FVector RotationAxis = FVector::CrossProduct(UpVector, NormalVector);
				RotationAxis.Normalize();
				float DotProduct = FVector::DotProduct(UpVector, NormalVector);
				float RotationAngle = acosf(DotProduct);
				FQuat Quat = FQuat(RotationAxis, RotationAngle);
				FQuat RootQuat = Mesh->GetActorQuat();
				FQuat NewQuat = Quat * RootQuat;
				Rotation = NewQuat.Rotator();
				Mesh->SetActorRotation(Rotation);
			}
			
			Mesh->SetMobility(EComponentMobility::Static);
			
			DetailActors.Add({Item->GetName(), Mesh->GetActorLocation(), Mesh});
		}
	}
}

void AEcoscapeTerrain::GenerateStartingItems()
{
	for (int i = 0; i < StartingItemCount; i++)
	{
		FTerrainStartingObjectType& Object = StartingItems[FMath::RandRange(0, StartingItems.Num() - 1)];

		AActor* SpawnedObject = nullptr;
		float X = FMath::RandRange(ExteriorTileCount + 2, Width - (ExteriorTileCount + 2));	
		float Y = FMath::RandRange(ExteriorTileCount + 2, Height - (ExteriorTileCount + 2));
		FVector Location = GetActorLocation() + FVector(X * Scale, Y * Scale, 100000);
		FHitResult Hit;
		if (GetWorld()->LineTraceSingleByChannel(Hit, Location, Location + FVector(0, 0, -200000), ECC_ITEM_PLACEABLE_ON))
		{
			if (Object.Item != nullptr)
			{
				if (Object.Item->bHasMaxWaterDepth && Water)
				{
					if (Object.Item->MaxWaterDepth > Hit.ImpactPoint.Z - Water->GetActorLocation().Z)
					{
						i--;
						continue;
					}
				}
				APlacedItem* Item = APlacedItem::SpawnItem(GetWorld(), Object.Item, Hit.ImpactPoint + FVector(Object.Item->ZOffset));

				TArray<FOverlapResult> Overlaps;
				Item->GetMesh()->ComponentOverlapMulti(Overlaps, GetWorld(), Item->GetMesh()->GetComponentLocation(), Item->GetMesh()->GetComponentRotation(), ECC_BLOCKS_ITEM_PLACEMENT);
				if (!Overlaps.IsEmpty())
				{
					Item->Destroy();
					i--;
					continue;
				}
				
				PlacedItems.Add(Item);
				Item->AssociatedTerrain = this;
				SpawnedObject = Item;
			}
			else if (Object.ActorType != nullptr)
			{
				if (0 > Hit.ImpactPoint.Z - Water->GetActorLocation().Z)
				{
					i--;
					continue;
				}
				SpawnedObject = GetWorld()->SpawnActor(Object.ActorType, &Hit.ImpactPoint, &FRotator::ZeroRotator);
				OtherSerialisedObjects.Add(SpawnedObject);
			} else
			{
				ECO_LOG_ERROR("In terrain, a placeable item was not either an item type or actor.");
				continue;
			}
		}

		if (!SpawnedObject)
			continue;

		SpawnedObject->AddActorWorldOffset(FVector(0, 0, UEcoscapeStatics::GetZUnderOrigin(SpawnedObject)));
		auto Rotation = UKismetMathLibrary::Conv_VectorToRotator(Hit.ImpactNormal);
		FVector UpVector = SpawnedObject->GetActorUpVector();
		FVector NormalVector = UKismetMathLibrary::VLerp(UpVector, Hit.ImpactNormal, 0.4f);
		FVector RotationAxis = FVector::CrossProduct(UpVector, NormalVector);
		RotationAxis.Normalize();
		float DotProduct = FVector::DotProduct(UpVector, NormalVector);
		float RotationAngle = acosf(DotProduct);
		FQuat Quat = FQuat(RotationAxis, RotationAngle);
		FQuat RootQuat = SpawnedObject->GetActorQuat();
		FQuat NewQuat = Quat * RootQuat;
		Rotation = NewQuat.Rotator();
		SpawnedObject->SetActorRotation(Rotation);
	}
}

void AEcoscapeTerrain::InitWalkable()
{
	Walkable.Init(false, (Width + 1) * (Height + 1));
	for (int X = 0; X < Width + 1; X++)
	{
		for (int Y = 0; Y < Height + 1; Y++)
		{
			int Index = GetVertexIndex(X, Y);
			Walkable[Index] = IsVertWalkable(Index);
		}
	}
}

void AEcoscapeTerrain::CreateMesh() const
{
	ProceduralMeshComponent->CreateMeshSection(0, Verticies, Triangles, Normals, UV0, VertexColors, Tangents, true);
	ProceduralMeshComponent->SetMaterial(0, Material);
}

void AEcoscapeTerrain::CalculateVertColour(int Index, bool Flush)
{
	const FVector Position = GetVertexPositionWorld(Index);

	const auto Coordinate = GetVertexXY(Index);
	const int Distance = FMath::Min(FMath::Min(Coordinate.X, Coordinate.Y), FMath::Min(Width - Coordinate.X, Height - Coordinate.Y));
	const float Alpha = 1 - (static_cast<float>(Distance - 3) / static_cast<float>(ExteriorTileCount - 3));
	VertexColors[Index] = UKismetMathLibrary::LinearColorLerp(FloorColour, ExteriorColour, FMath::Clamp(Alpha * 7, 0, 1)).ToFColor(false);
	
	for (const APlacedItem* Item : PlacedItems)
	{
		const float DistSquared = FVector::DistSquared(Position, Item->GetActorLocation());
		const auto Data = Item->GetItemData();
		// Only using the X scale here. Assumes uniform scale
		const FColor Remainder = Data->MaxLandColour.ToFColor(false) - VertexColors[Index];
		const FColor Offset = UEcoscapeStatics::ClampColor(
			Data->LandColour.ToFColor(false) * FMath::Clamp(1 - (DistSquared / (Data->ColourRange * Data->ColourRange)), 0, 1),
			FColor(0, 0, 0), Remainder);

		if (Data->bNegativeColour)
			VertexColors[Index] -= Offset;
		else
			VertexColors[Index] += Offset;
	}
	VertexColors[Index] = UEcoscapeStatics::AddToColor(VertexColors[Index], ColorOffsets[Index]);
	
	if (Flush)
		FlushMesh();
}

// TODO: Maybe add a faster approximate function?
FVector AEcoscapeTerrain::GetCenterPosition()
{
	const FVector Center = GetActorLocation() + FVector((Width * Scale) / 2, (Height * Scale) / 2, 3000);
	FHitResult Hit;
	GetWorld()->LineTraceSingleByChannel(Hit, Center, Center + (FVector::DownVector * 4000), ECC_WorldStatic);
	return Hit.ImpactPoint;
}

void AEcoscapeTerrain::GetXYBounds(FVector2D& Min, FVector2D& Max)
{
	FVector Loc = GetActorLocation();
	Min = FVector2D(Loc.X, Loc.Y);
	Loc += FVector(Width * Scale, Height * Scale, 0);
	Max = FVector2D(Loc.X, Loc.Y);
}

int AEcoscapeTerrain::GetClosestVertex(FVector Position)
{
	SCOPE_CYCLE_COUNTER(STAT_GetNearestVert);
	
	const FVector ActorLoc = GetActorLocation();
	const float OriginX = ActorLoc.X;
	const float EndX = OriginX + (Width * Scale);
	const float OriginY = ActorLoc.Y;
	const float EndY = OriginY + (Height * Scale);

	// Using these two lines would be shorter, but I think slower, and this is quite a hot path
	// const float XAlpha = FMath::GetMappedRangeValueUnclamped(FVector2D(ActorLoc.X, ActorLoc.X + (Width * Scale)), FVector2D(0, 1), Position.X);
	// const float YAlpha = FMath::GetMappedRangeValueUnclamped(FVector2D(ActorLoc.Y, ActorLoc.Y + (Height * Scale)), FVector2D(0, 1), Position.Y);
	const float XAlpha = FMath::Clamp((Position.X - OriginX) / (EndX - OriginX), 0, 1);
	const float YAlpha = FMath::Clamp((Position.Y - OriginY) / (EndY - OriginY), 0, 1);

	const int X = FMath::RoundToInt(Width  * XAlpha);
	const int Y = FMath::RoundToInt(Height * YAlpha);

	return GetVertexIndex(X, Y);
}

bool AEcoscapeTerrain::IsPositionWithinPlayableSpace(FVector Position)
{
	const FVector Loc = GetActorLocation();
	return FMath::IsWithin(Position.X, Loc.X + (ExteriorTileCount * Scale), Loc.X + (Width * Scale) - (ExteriorTileCount * Scale))
		&& FMath::IsWithin(Position.Y, Loc.Y + (ExteriorTileCount * Scale), Loc.Y + (Height * Scale) - (ExteriorTileCount * Scale));
}

AProceduralFenceMesh* AEcoscapeTerrain::CreateFence(FVector2D Start, FVector2D End, int Step)
{
	const int LowX  = Start.X < End.X ? Start.X : End.X;
	const int LowY  = Start.Y < End.Y ? Start.Y : End.Y;
	const int HighX = Start.X > End.X ? Start.X : End.X;
	const int HighY = Start.Y > End.Y ? Start.Y : End.Y;
	
	int X = LowX, Y = LowY;
	AProceduralFenceMesh* Fence = GetWorld()->SpawnActor<AProceduralFenceMesh>(FenceClass, GetVertexPositionWorld(GetVertexIndex(X, Y)), FRotator::ZeroRotator);
	Fence->Start = Start;
	Fence->End = End;
	Fence->bShouldGenerateGate = true;
	Fence->AssociatedTerrain = this;
	Fence->BottomSplineComponent->ClearSplinePoints();
	Fence->BottomSplineComponent->AddSplineWorldPoint(GetVertexPositionWorld(GetVertexIndex(X, Y)) + FVector(0, 0, 30));
	Fence->TopSplineComponent->ClearSplinePoints();
	Fence->TopSplineComponent->AddSplineWorldPoint(GetVertexPositionWorld(GetVertexIndex(X, Y)) + FVector(0, 0, 250));

	for (; X < HighX; X = FMath::Min(HighX, X + Step))
	{
		FVector Position = GetVertexPositionWorld(GetVertexIndex(X, Y)) + FVector(0, 0, 30);
		Fence->BottomSplineComponent->AddSplineWorldPoint(Position);
		FVector TopPosition = Position + FVector(0, 0, 250);
		if (bEnableWater)
			TopPosition.Z = FMath::Max(TopPosition.Z, Water->GetActorLocation().Z + 250);
		Fence->TopSplineComponent->AddSplineWorldPoint(TopPosition);
	}
	for (; Y < HighY; Y = FMath::Min(HighY, Y + Step))
	{
		FVector Position = GetVertexPositionWorld(GetVertexIndex(X, Y)) + FVector(0, 0, 30);
		Fence->BottomSplineComponent->AddSplineWorldPoint(Position);
		FVector TopPosition = Position + FVector(0, 0, 250);
		if (bEnableWater)
			TopPosition.Z = FMath::Max(TopPosition.Z, Water->GetActorLocation().Z + 250);
		Fence->TopSplineComponent->AddSplineWorldPoint(TopPosition);
	}
	for (; X > LowX; X = FMath::Max(LowX, X - Step))
	{
		FVector Position = GetVertexPositionWorld(GetVertexIndex(X, Y)) + FVector(0, 0, 30);
		Fence->BottomSplineComponent->AddSplineWorldPoint(Position);
		FVector TopPosition = Position + FVector(0, 0, 250);
		if (bEnableWater)
			TopPosition.Z = FMath::Max(TopPosition.Z, Water->GetActorLocation().Z + 250);
		Fence->TopSplineComponent->AddSplineWorldPoint(TopPosition);
	}
	for (; Y > LowY; Y = FMath::Max(LowY, Y - Step))
	{
		FVector Position = GetVertexPositionWorld(GetVertexIndex(X, Y)) + FVector(0, 0, 30);
		Fence->BottomSplineComponent->AddSplineWorldPoint(Position);
		FVector TopPosition = Position + FVector(0, 0, 250);
		if (bEnableWater)
			TopPosition.Z = FMath::Max(TopPosition.Z, Water->GetActorLocation().Z + 250);
		Fence->TopSplineComponent->AddSplineWorldPoint(TopPosition);
	}

	X = LowX;
	Y = LowY;
	Fence->BottomSplineComponent->AddSplineWorldPoint(GetVertexPositionWorld(GetVertexIndex(X, Y)) + FVector(0, 0, 30));	
	Fence->TopSplineComponent->AddSplineWorldPoint(GetVertexPositionWorld(GetVertexIndex(X, Y)) + FVector(0, 0, 250));	
	
	Fence->Regenerate();

	PlacedFences.Add(Fence);

	OnFencePlaced(Start, End);
	
	return Fence;
}

TArray<FVertexOverlapInfo> AEcoscapeTerrain::GetVerticiesInSphere(FVector Position, float Radius, bool CheckZ)
{
	SCOPE_CYCLE_COUNTER(STAT_VertsInSphere);

	// DrawDebugSphere(GetWorld(), Position, Radius, 30, FColor::Red);
	
	TArray<FVertexOverlapInfo> Indicies;

	const int RootVertex = GetClosestVertex(Position);
	const int IndexRadius = (Radius / Scale) + 1; // Add 1 so verts that should be inside but aren't due to offsets from the root vertex method are included.

	for (int CurrentRadius = 0; CurrentRadius < IndexRadius; CurrentRadius++)
	{
		// Special case for the root vertex. Just test distance.
		if (CurrentRadius == 0)
		{
			const float Dist = CheckZ ? FVector::Dist(Position, GetVertexPositionWorld(RootVertex)) : FVector::DistXY(Position, GetVertexPositionWorld(RootVertex));
			if (Dist <= Radius)
				Indicies.Add(FVertexOverlapInfo { RootVertex, Dist });
			if (!CheckZ && Dist > Radius)
				return Indicies;
			continue;
		}

		for (int X = -CurrentRadius; X <= CurrentRadius; X++)
		{
			for (int Y = -CurrentRadius; Y <= CurrentRadius; Y++)
			{
				const int Index = RootVertex + X + (Y * (Width + 1));
				
				// Ensure no out of bounds indicies are given
				if (Index < 0 || Index >= Verticies.Num())
					continue;

				const float Dist = CheckZ ? FVector::Dist(Position, GetVertexPositionWorld(Index)) : FVector::DistXY(Position, GetVertexPositionWorld(Index));
				if (Dist <= Radius)
					Indicies.Add(FVertexOverlapInfo { Index, Dist });
			}
		}
	}
	
	return Indicies;
}

FVector AEcoscapeTerrain::FindSpawnPoint()
{
	int Tries = 75;
	FVector Current;

	const FCollisionShape CollisionCheckShape = FCollisionShape::MakeBox(FVector(50, 50, 100));
	
	while (Tries > 0)
	{
		Current = GetVertexPositionWorld(GetVertexIndex(
			FMath::RandRange(ExteriorTileCount + 2, Width - (ExteriorTileCount + 2)),
			FMath::RandRange(ExteriorTileCount + 2, Height - (ExteriorTileCount + 2))));

		bool bValid = true;

		// Check it's not underwater
		if (Water)
		{
			if (Current.Z <= Water->GetActorLocation().Z)
				bValid = false;
		}

		// Check it doesn't collide with any objects
		// DrawDebugBox(GetWorld(), Current + FVector(0, 0, 110), CollisionCheckShape.GetExtent(), FColor::Red, false, 5);
		if (bValid && GetWorld()->OverlapBlockingTestByProfile(Current + FVector(0, 0, 110), FQuat::Identity, UCollisionProfile::Pawn_ProfileName, CollisionCheckShape))
			bValid = false;

		if (bValid)
		{
			// UE_LOG(LogEcoscape, Log, TEXT("Took %i tries to find spawn pos."), 75 - Tries);
			return Current;
		}
			
		Tries--;
	}
	
	UE_LOG(LogEcoscape, Log, TEXT("Didn't find valid spawn."));
	return Current;
}

void AEcoscapeTerrain::AddVertexColour(int Index, FColor AddedColor, bool Flush)
{
	SCOPE_CYCLE_COUNTER(STAT_AddVertColor);
	
	if (Index < 0 || Index >= Verticies.Num())
	{
		UE_LOG(LogEcoscape, Error, TEXT("AEcoscapeTerrain::AddVertexColour called with out of range index %i (range is 0..%i)"), Index, Verticies.Num());
		return;	
	}

	VertexColors[Index] += AddedColor;
	if (Flush)
		ProceduralMeshComponent->UpdateMeshSection(0, Verticies, Normals, UV0, VertexColors, Tangents);
}

void AEcoscapeTerrain::FlushMesh()
{
	ProceduralMeshComponent->UpdateMeshSection(0, Verticies, Normals, UV0, VertexColors, Tangents);
}

#if WITH_EDITOR
void AEcoscapeTerrain::DrawVerticies()
{
	for (int i = 0; i < Verticies.Num(); i++)
		DrawDebugSphere(GetWorld(), GetVertexPositionWorld(i), 20.0f, 8, FColor::Red, false, 5);
}

void AEcoscapeTerrain::DrawIndicies()
{
	for (int i = 0; i < Verticies.Num(); i++)
		DrawDebugString(GetWorld(), GetVertexPositionWorld(i) + FVector(0, 0, 100), FString::FromInt(i), nullptr, FColor::White, 3);
}

void AEcoscapeTerrain::DrawDrinkLocations()
{
	for (const auto& DrinkLoc : DrinkLocations)
	{
		DrawDebugSphere(GetWorld(), DrinkLoc.Location, 20, 6, FColor::Red, false, 5);
		DrawDebugDirectionalArrow(GetWorld(), DrinkLoc.Location + FVector(0, 0, 50) - (DrinkLoc.DrinkerOrientation * 100),
		                          DrinkLoc.Location + FVector(0, 0, 50) + (DrinkLoc.DrinkerOrientation * 100), 5, FColor::Red, false, 5);
	}
}

void AEcoscapeTerrain::DrawWalkability()
{
	for (int i = 0; i < Verticies.Num(); i++)
	{
		if (IsPositionWithinPlayableSpace(GetVertexPositionWorld(i)))
			DrawDebugSphere(GetWorld(), GetVertexPositionWorld(i), 30, 6, Walkable[i] ? FColor::Green : FColor::Red, false, 3);
	}
}
#endif

void AEcoscapeTerrain::Regenerate()
{
	SCOPE_CYCLE_COUNTER(STAT_GenTerrain);
	
	// Re-roll noise seeds	
	for (FTerrainNoiseLayer& Layer : NoiseLayers)
		Layer.Seed = FMath::RandRange(0.0f, 1000000.0f);
	ColorOffsetSeed = FMath::RandRange(0.0f, 1000000.0f);

	if (NavMeshVolume)
	{
		NavMeshVolume->Destroy();
		NavMeshVolume = nullptr;
	}

	if (Water)
	{
		Water->Destroy();
		Water = nullptr;
	}
	
	ResetMeshData();
	for (APlacedItem* Item : PlacedItems)
		Item->Destroy();
	PlacedItems.Empty();
	for (AProceduralFenceMesh* Fence : PlacedFences)
		Fence->Destroy();
	PlacedFences.Empty();
	for (const auto& Detail : DetailActors)
		if (Detail.Actor) Detail.Actor->Destroy();
	DetailActors.Empty();
	for (AActor* Item : OtherSerialisedObjects)
		if (Item) Item->Destroy();
	OtherSerialisedObjects.Empty();
	GenerateVerticies();
	GenerateIndicies();
	GenerateNormals();

	// Generate water
	if (bEnableWater)
	{
		FVector WaterPosition = GetActorLocation() + FVector((Width * Scale) / 2, (Height * Scale) / 2, 3000);
		WaterPosition.Z = Heights[static_cast<int>(static_cast<float>(Heights.Num() - 1) * WaterHeight)]; // Clamp as * 1 occasionally puts it at size + 1
		Water = GetWorld()->SpawnActor<AActor>(WaterClass, WaterPosition, FRotator::ZeroRotator);
		GenerateDrinkLocations();
	}
	
	GenerateFence();

	if (TerrainName == "Desert")
	{
		// If we're desert, generate the oasis hole
		const FVector Center = GetActorLocation() + FVector(Width * Scale / 2, Height * Scale / 2, 0);
		const double Target = static_cast<double>(LowestHeight) - 250;
		const float Radius = 8 * Scale;
		auto Verts = GetVerticiesInSphere(Center, Radius);

		for (const auto& Vert : Verts)
		{
			FVector Pos = Verticies[Vert.Index];
			const float Distance = Vert.Distance / Radius;
			Pos.Z = FMath::InterpEaseOut(Target, Pos.Z, Distance, 3);
			Verticies[Vert.Index] = Pos;
		}
	}

	CreateMesh();
	GenerateExteriorDetail();
	GenerateStartingItems();

	// Recalculate colours
	for (int X = ExteriorTileCount; X <= Width - ExteriorTileCount; X++)
		for (int Y = ExteriorTileCount; Y <= Height - ExteriorTileCount; Y++)
			CalculateVertColour(GetVertexIndex(X, Y));
	FlushMesh();

	InitWalkable();
	
	// Update the navigation
	FNavigationSystem::UpdateComponentData(*ProceduralMeshComponent);
}
