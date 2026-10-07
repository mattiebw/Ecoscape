// copyright lololol

#include "World/Fence/ProceduralFenceMesh.h"

#include "Ecoscape.h"
#include "EcoscapeStatics.h"
#include "KismetProceduralMeshLibrary.h"
#include "UObject/ConstructorHelpers.h"
#include "World/EcoscapeProcMeshStatics.h"
#include "World/Fence/FenceGate.h"
#include "World/EcoscapeTerrain.h" // Not unused, used for archive overload

#if WITH_EDITOR
#include "MessageLogModule.h"
#endif

// Sets default values
AProceduralFenceMesh::AProceduralFenceMesh()
{
	PrimaryActorTick.bCanEverTick = false;

	ProceduralMeshComponent = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Mesh"));
	ProceduralMeshComponent->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
	ProceduralMeshComponent->SetCollisionResponseToChannel(ECC_BLOCKS_ITEM_PLACEMENT, ECR_Block);
	ProceduralMeshComponent->SetCollisionResponseToChannel(ECC_HIGHLIGHTABLE, ECR_Block);
	ProceduralMeshComponent->SetCollisionResponseToChannel(ECC_BLOCKS_HABITAT, ECR_Block);
	ProceduralMeshComponent->SetMaterial(0, Material);
	ProceduralMeshComponent->ComponentTags.Add("Outline");
	BottomSplineComponent = CreateDefaultSubobject<USplineComponent>(TEXT("BottomPoints"));
	BottomSplineComponent->SetupAttachment(ProceduralMeshComponent);
	TopSplineComponent = CreateDefaultSubobject<USplineComponent>(TEXT("TopPoints"));
	TopSplineComponent->SetupAttachment(ProceduralMeshComponent);
	RootComponent = ProceduralMeshComponent;

	const auto MatFinder = ConstructorHelpers::FObjectFinder<UMaterialInterface>(TEXT("Material'/Game/Materials/M_Fence.M_Fence'"));
	if (MatFinder.Object != nullptr)
    	Material = MatFinder.Object;
}

void AProceduralFenceMesh::Regenerate()
{
	Verticies.Empty();
	Indicies.Empty();
	UV0.Empty();
	Normals.Empty();
	Tangents.Empty();
	ProceduralMeshComponent->SetMaterial(0, Material);
	
	FVector LastBottomPosition = FVector::ZeroVector, LastTopPosition = FVector::ZeroVector;

	if (BottomSplineComponent->GetNumberOfSplinePoints() != TopSplineComponent->GetNumberOfSplinePoints())
	{
		ECO_LOG_ERROR(
			FString::Printf(TEXT(
					"Fence %s has differing point counts on the top and bottom splines! (Top has %i, bottom has %i)"), 
				*GetName(), TopSplineComponent->GetNumberOfSplinePoints(), BottomSplineComponent->GetNumberOfSplinePoints()
			));
		return;
	}
	
	int NumPoints = BottomSplineComponent->GetNumberOfSplinePoints();
	for (int i = 0; i < NumPoints; i++)
	{
		bool ShouldPlace = true;

		int GateCount = bShouldGenerateGate ? FMath::Max(1, NumPoints / 8) : 0;
		int GateEvery = GateCount == 0 ? 0 : NumPoints / GateCount; // Ternary to prevent div by 0
		
		if (i == 0 || ShouldPlace)
		{
			FVector BottomPos = BottomSplineComponent->GetLocationAtSplinePoint(i, ESplineCoordinateSpace::Local);
			FVector TopPos = TopSplineComponent->GetLocationAtSplinePoint(i, ESplineCoordinateSpace::Local);
			const float ZDiff = TopPos.Z - BottomPos.Z;
			UEcoscapeProcMeshStatics::AddCuboid(Verticies, Indicies, UV0, Normals, Tangents, BottomPos + FVector(0, 0, -5 + (ZDiff / 2)), FVector(30, 30, (ZDiff / 2) + 80), true, FVector2D(0.15, 0.03));

			if (i != 0)
			{
				if (GateCount > 0 && (i + GateEvery / 2) % GateEvery == 0)
				{	
					AFenceGate* FenceGate = GetWorld()->SpawnActor<AFenceGate>(FenceGateClass, FVector::ZeroVector, FRotator::ZeroRotator);
					FenceGate->Create(GetActorLocation() + LastBottomPosition, GetActorLocation() + BottomPos);
					Gates.Add(FenceGate);
					
					GateCount--;
				} else {
					// Gen fence and wire
					UEcoscapeProcMeshStatics::AddCuboid(Verticies, Indicies, UV0, Normals, Tangents, LastTopPosition, TopPos, FVector2D(20, 20), true,
							  FVector2D(0.01, 0.01));

					int VertIndex = Verticies.Num();
					const float Distance = FVector::Distance(LastBottomPosition, BottomPos);
					Verticies.Add(LastBottomPosition - FVector(0, 0, 80));
					Verticies.Add(LastTopPosition + FVector(0, 0, -50));
					Verticies.Add(BottomPos - FVector(0, 0, 80));
					Verticies.Add(TopPos + FVector(0, 0, -50));
					Indicies.Append({ VertIndex + 2, VertIndex + 3, VertIndex + 1, VertIndex + 2, VertIndex + 1, VertIndex });
					UV0.Append({ FVector2D(0, 0.0625), FVector2D(0, .5), FVector2D(Distance / 200, 0.0625), FVector2D(Distance / 200, .5) });
				}
			}
			
			LastBottomPosition = BottomPos;
			LastTopPosition    = TopPos;
		}
	}

	CalculateNormals();
	
	ProceduralMeshComponent->CreateMeshSection(0, Verticies, Indicies, Normals, UV0, TArray<FColor>(), Tangents, true);
}

void AProceduralFenceMesh::Destroyed()
{
	for (AFenceGate* Gate : Gates)
		Gate->Destroy();
	Super::Destroyed();
}

void AProceduralFenceMesh::SerialiseFence(FArchive& Ar)
{
	Ar << Verticies;
	Ar << Indicies;
	Ar << Normals;
	Ar << UV0;
	Ar << Tangents;

	Ar << Start;
	Ar << End;
	
	if (Ar.IsLoading())
		ProceduralMeshComponent->CreateMeshSection(0, Verticies, Indicies, Normals, UV0, TArray<FColor>(), Tangents, true);

	int GateCount = Gates.Num();
	Ar << GateCount;

	for (int i = 0; i < GateCount; i++)
	{
		if (Ar.IsSaving())
		{
			FVector Loc = Gates[i]->GetActorLocation(); 
			Ar << Loc;

			Gates[i]->SerialiseGate(Ar);
		} else
		{
			FVector Loc;
			Ar << Loc;

			AFenceGate* Gate = GetWorld()->SpawnActor<AFenceGate>(FenceGateClass, Loc, FRotator::ZeroRotator);
			Gate->SerialiseGate(Ar);
			Gates.Add(Gate);
		}
	}
}

#if WITH_EDITOR
void AProceduralFenceMesh::DrawVerticies()
{
	for (int i = 0; i < Verticies.Num(); i++)
	{
		DrawDebugSphere(GetWorld(), GetActorLocation() + Verticies[i], 20, 6, FColor::Red, false, 2);
		DrawDebugString(GetWorld(), GetActorLocation() + Verticies[i] + FVector(0, 0, 100), FString::FromInt(i),
		                nullptr, FColor::White, 2);
	}
}
#endif

void AProceduralFenceMesh::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	Regenerate();
}

void AProceduralFenceMesh::CalculateNormals()
{
	// BEEFY SOLUTION
	Normals.Init(FVector::ZeroVector, Verticies.Num());

	for (int i = 0; i < Indicies.Num(); i += 3)
	{
		const FVector P = FVector::CrossProduct(Verticies[Indicies[i + 1]] - Verticies[Indicies[i]], Verticies[Indicies[i + 2]] - Verticies[Indicies[i]]);
		Normals[Indicies[i]] += P;
		Normals[Indicies[i + 1]] += P;
		Normals[Indicies[i + 2]] += P;
	}

	for (int i = 0; i < Normals.Num(); i++)
	{
		Normals[i].Normalize();
		Normals[i] = -Normals[i];
	}
}
