// Copyright Project Borealis
// with edits by Matt Ware

#include "Character/EcoscapeFPPlayerCharacter.h"

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 1
#include "Engine/DamageEvents.h"
#endif

#include "EcoscapeLog.h"
#include "EcoscapeStatics.h"
#include "Components/CapsuleComponent.h"
#include "HAL/IConsoleManager.h"

#include "../../Public/Character/EcoscapeFPPlayerMovement.h"
#include "Character/EcoscapePlayerController.h"
#include "GameFramework/DamageType.h"
#include "Kismet/KismetMathLibrary.h"
#include "World/InteractableComponent.h"

static TAutoConsoleVariable<int32> CVarAutoBHop(TEXT("move.Pogo"), 0, TEXT("If holding spacebar should make the player jump whenever possible.\n"), ECVF_Cheat);
static TAutoConsoleVariable<int32> CVarJumpBoost(TEXT("move.JumpBoost"), 1, TEXT("If the player should boost in a movement direction while jumping.\n0 - disables jump boosting entirely\n1 - boosts in the direction of input, even when moving in another direction\n2 - boosts in the direction of input when moving in the same direction\n"), ECVF_Cheat);
static TAutoConsoleVariable<int32> CVarBunnyhop(TEXT("move.Bunnyhopping"), 0, TEXT("Enable normal bunnyhopping.\n"), ECVF_Cheat);

void AEcoscapeFPPlayerCharacter::HighlightObject(UOutlineComponent* NewOutline)
{
	if (OutlinedObject)
		OutlinedObject->HideOutline();
	OutlinedObject = NewOutline;
	if (OutlinedObject)
		OutlinedObject->ShowOutline();
}

// Sets default values
AEcoscapeFPPlayerCharacter::AEcoscapeFPPlayerCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UEcoscapeFPPlayerMovement>(ACharacter::CharacterMovementComponentName))
{
	PrimaryActorTick.bCanEverTick = true;

	// Set size for collision capsule
	GetCapsuleComponent()->InitCapsuleSize(30.48f, 68.58f);
	// Set collision settings. We are the invisible player with no 3rd person mesh.
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Camera, ECR_Block);
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_BLOCKS_GROWTH, ECR_Block);

	// set our turn rates for input
	BaseTurnRate = 45.0f;
	BaseLookUpRate = 45.0f;

	// Camera eye level
	DefaultBaseEyeHeight = 53.34f;
	BaseEyeHeight = DefaultBaseEyeHeight;
	constexpr float CrouchedHalfHeight = 68.58f / 2.0f;
	CrouchedEyeHeight = 53.34f - CrouchedHalfHeight;

	// Fall Damage Initializations
	// PLAYER_MAX_SAFE_FALL_SPEED
	MinSpeedForFallDamage = 1002.9825f;
	// PLAYER_MIN_BOUNCE_SPEED
	MinLandBounceSpeed = 329.565f;

	// get pointer to movement component
	MovementPtr = Cast<UEcoscapeFPPlayerMovement>(ACharacter::GetMovementComponent());

	CapDamageMomentumZ = 476.25f;
}

void AEcoscapeFPPlayerCharacter::BeginPlay()
{
	// Call the base class
	Super::BeginPlay();
	// Max jump time to get to the top of the arc
	MaxJumpTime = -4.0f * GetCharacterMovement()->JumpZVelocity / (3.0f * GetCharacterMovement()->GetGravityZ());
}

void AEcoscapeFPPlayerCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (bDeferJumpStop)
	{
		bDeferJumpStop = false;
		Super::StopJumping();
	}

	// Check for interactions
	if (bCurrentlyPossessed || true)
	{
		FVector EyeHeight = GetActorLocation() + FVector(0, 0, 45);
		FVector LookDir = UKismetMathLibrary::Conv_RotatorToVector(GetViewRotation());

		FHitResult Hit;
		FCollisionQueryParams Params;
		Params.AddIgnoredActor(this);
		UInteractionPrompt* InteractionPrompt = Cast<AEcoscapePlayerController>(Controller)->GetInteractionPrompt();
		if (GetWorld()->LineTraceSingleByChannel(Hit, EyeHeight, EyeHeight + (LookDir * InteractionRange), ECC_WorldStatic, Params))
		{
			UOutlineComponent* Outline = Cast<UOutlineComponent>(Hit.GetActor()->GetComponentByClass(UOutlineComponent::StaticClass()));

			UInteractableComponent* Interactable = Cast<UInteractableComponent>(Hit.GetActor()->GetComponentByClass(UInteractableComponent::StaticClass()));
			if (Interactable && Interactable->bCanInteract)
			{
				if (Outline)
					HighlightObject(Outline);
				CurrentInteractable = Interactable;
				InteractionPrompt->ShowPrompt(Interactable);
			} 
			else
			{
				CurrentInteractable = nullptr;
				HighlightObject(nullptr);
				if (InteractionPrompt->IsPromptVisible())
					InteractionPrompt->HidePrompt();
			}
		} else
		{
			if (InteractionPrompt->IsPromptVisible())
				InteractionPrompt->HidePrompt();
			CurrentInteractable = nullptr;
			HighlightObject(nullptr);
		}
	}
}

void AEcoscapeFPPlayerCharacter::ApplyDamageMomentum(float DamageTaken, FDamageEvent const& DamageEvent, APawn* PawnInstigator, AActor* DamageCauser)
{
	UDamageType const* const DmgTypeCDO = DamageEvent.DamageTypeClass->GetDefaultObject<UDamageType>();
	if (GetCharacterMovement())
	{
		FVector ImpulseDir;

		if (IsValid(DamageCauser))
		{
			ImpulseDir = (GetActorLocation() - DamageCauser->GetActorLocation()).GetSafeNormal();
		}
		else
		{
			FHitResult HitInfo;
			DamageEvent.GetBestHitInfo(this, DamageCauser, HitInfo, ImpulseDir);
		}

		const float SizeFactor = (60.96f * 60.96f * 137.16f) / (FMath::Square(GetCapsuleComponent()->GetScaledCapsuleRadius() * 2.0f) * GetCapsuleComponent()->GetScaledCapsuleHalfHeight() * 2.0f);

		float Magnitude = 1.905f * DamageTaken * SizeFactor * 5.0f;
		Magnitude = FMath::Min(Magnitude, 1905.0f);

		FVector Impulse = ImpulseDir * Magnitude;
		bool const bMassIndependentImpulse = !DmgTypeCDO->bScaleMomentumByMass;
		float MassScale = 1.f;
		if (!bMassIndependentImpulse && GetCharacterMovement()->Mass > SMALL_NUMBER)
		{
			MassScale = 1.f / GetCharacterMovement()->Mass;
		}
		if (CapDamageMomentumZ > 0.f)
		{
			Impulse.Z = FMath::Min(Impulse.Z * MassScale, CapDamageMomentumZ) / MassScale;
		}

		GetCharacterMovement()->AddImpulse(Impulse, bMassIndependentImpulse);
	}
}

void AEcoscapeFPPlayerCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	bCurrentlyPossessed = true;
	if (AEcoscapePlayerController* PlayerController = Cast<AEcoscapePlayerController>(NewController))
	{
		PlayerController->SetMouseEnabled(false);
		PlayerController->CenterMouse();
	}
	else
		UE_LOG(LogEcoscape, Error, TEXT("Ecoscape pawn possessed by non-ecoscape controller!"));
}

void AEcoscapeFPPlayerCharacter::Interact()
{
	if (CurrentInteractable)
		CurrentInteractable->OnInteract();
}

void AEcoscapeFPPlayerCharacter::ClearJumpInput(float DeltaTime)
{
	// Don't clear jump input right away if we're auto hopping or noclipping (holding to go up), or if we are deferring a jump stop
	if (CVarAutoBHop.GetValueOnGameThread() != 0 || bAutoBunnyhop || GetCharacterMovement()->bCheatFlying || bDeferJumpStop)
	{
		return;
	}
	Super::ClearJumpInput(DeltaTime);
}

void AEcoscapeFPPlayerCharacter::Jump()
{
	if (GetCharacterMovement()->IsFalling())
	{
		bDeferJumpStop = true;
	}

	Super::Jump();
}

void AEcoscapeFPPlayerCharacter::OnMovementModeChanged(EMovementMode PrevMovementMode, uint8 PrevCustomMode)
{
	if (!bPressedJump)
	{
		ResetJumpState();
	}

	if (GetCharacterMovement()->IsFalling())
	{
		// Record jump force start time for proxies. Allows us to expire the jump even if not continually ticking down a timer.
		if (bProxyIsJumpForceApplied)
		{
			ProxyJumpForceStartedTime = GetWorld()->GetTimeSeconds();
		}
	}
	else
	{
		JumpCurrentCount = 0;
		JumpKeyHoldTime = 0.0f;
		JumpForceTimeRemaining = 0.0f;
		bWasJumping = false;
	}

	K2_OnMovementModeChanged(PrevMovementMode, GetCharacterMovement()->MovementMode, PrevCustomMode, GetCharacterMovement()->CustomMovementMode);
	MovementModeChangedDelegate.Broadcast(this, PrevMovementMode, PrevCustomMode);
}

void AEcoscapeFPPlayerCharacter::StopJumping()
{
	if (!bDeferJumpStop)
	{
		Super::StopJumping();
	}
}

void AEcoscapeFPPlayerCharacter::OnJumped_Implementation()
{
	const int32 JumpBoost = CVarJumpBoost->GetInt();
	if (MovementPtr->IsOnLadder())
	{
		return;
	}

	if (GetWorld()->GetTimeSeconds() >= LastJumpBoostTime + MaxJumpTime && JumpBoost)
	{
		LastJumpBoostTime = GetWorld()->GetTimeSeconds();
		// Boost forward speed on jump
		const FVector Facing = GetActorForwardVector();
		// Use input direction
		FVector Input = GetCharacterMovement()->GetCurrentAcceleration();
		if (JumpBoost != 1)
		{
			// Only boost input in the direction of current movement axis (prevents ABH).
			Input *= FMath::Max(Input.GetSafeNormal2D() | GetCharacterMovement()->Velocity.GetSafeNormal2D(), 0.0f);
		}
		const float ForwardSpeed = Input | Facing;
		// Adjust how much the boost is
		const float SpeedBoostPercent = bIsSprinting || bIsCrouched ? 0.1f : 0.5f;
		// How much we are boosting by
		float SpeedAddition = FMath::Abs(ForwardSpeed * SpeedBoostPercent);
		// We can only boost up to this much
		const float MaxBoostedSpeed = GetCharacterMovement()->GetMaxSpeed() + GetCharacterMovement()->GetMaxSpeed() * SpeedBoostPercent;
		// Calculate new speed
		const float NewSpeed = SpeedAddition + GetMovementComponent()->Velocity.Size2D();
		float SpeedAdditionNoClamp = SpeedAddition;

		// Scale the boost down if we are going over
		if (NewSpeed > MaxBoostedSpeed)
		{
			SpeedAddition -= NewSpeed - MaxBoostedSpeed;
		}

		if (ForwardSpeed < -MovementPtr->GetMaxAcceleration() * FMath::Sin(0.6981f))
		{
			// Boost backwards if we're going backwards
			SpeedAddition *= -1.0f;
			SpeedAdditionNoClamp *= -1.0f;
		}

		// Boost our velocity
		FVector JumpBoostedVel = GetMovementComponent()->Velocity + Facing * SpeedAddition;
		float JumpBoostedSizeSq = JumpBoostedVel.SizeSquared2D();
		if (CVarBunnyhop.GetValueOnGameThread() != 0)
		{
			const FVector JumpBoostedUnclampVel = GetMovementComponent()->Velocity + Facing * SpeedAdditionNoClamp;
			const float JumpBoostedUnclampSizeSq = JumpBoostedUnclampVel.SizeSquared2D();
			if (JumpBoostedUnclampSizeSq > JumpBoostedSizeSq)
			{
				JumpBoostedVel = JumpBoostedUnclampVel;
				JumpBoostedSizeSq = JumpBoostedUnclampSizeSq;
			}
		}
		if (GetMovementComponent()->Velocity.SizeSquared2D() < JumpBoostedSizeSq)
		{
			GetMovementComponent()->Velocity = JumpBoostedVel;
		}
	}
}

// ReSharper disable once CppMemberFunctionMayBeConst
void AEcoscapeFPPlayerCharacter::ToggleNoClip()
{
	MovementPtr->ToggleNoClip();
}

// Sample for multiplayer games with a Mesh3P with crouch support
#if 0
void APBPlayerCharacter::OnEndCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust)
{
	Super::OnEndCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust);
	const APBPlayerCharacter* DefaultChar = GetDefault<APBPlayerCharacter>(GetClass());
	if (Mesh3P && DefaultChar->Mesh3P)
	{
		FVector MeshRelativeLocation = Mesh3P->GetRelativeLocation();
		MeshRelativeLocation.Z = DefaultChar->Mesh3P->GetRelativeLocation().Z - ScaledHalfHeightAdjust;
		Mesh3P->SetRelativeLocation(MeshRelativeLocation);
	}
}

void APBPlayerCharacter::OnStartCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust)
{
	Super::OnStartCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust);
	const APBPlayerCharacter* DefaultChar = GetDefault<APBPlayerCharacter>(GetClass());
	if (Mesh3P && DefaultChar->Mesh3P)
	{
		FVector MeshRelativeLocation = Mesh3P->GetRelativeLocation();
		MeshRelativeLocation.Z = DefaultChar->Mesh3P->GetRelativeLocation().Z + ScaledHalfHeightAdjust;
		Mesh3P->SetRelativeLocation(MeshRelativeLocation);
	}
}
#endif

bool AEcoscapeFPPlayerCharacter::CanJumpInternal_Implementation() const
{
	// UE-COPY: ACharacter::CanJumpInternal_Implementation()

	bool bCanJump = GetCharacterMovement() && GetCharacterMovement()->IsJumpAllowed();

	if (bCanJump)
	{
		// Ensure JumpHoldTime and JumpCount are valid.
		if (!bWasJumping || GetJumpMaxHoldTime() <= 0.0f)
		{
			if (JumpCurrentCount == 0 && GetCharacterMovement()->IsFalling())
			{
				bCanJump = JumpCurrentCount + 1 < JumpMaxCount;
			}
			else
			{
				bCanJump = JumpCurrentCount < JumpMaxCount;
			}
		}
		else
		{
			// Only consider JumpKeyHoldTime as long as:
			// A) We are on the ground
			// B) The jump limit hasn't been met OR
			// C) The jump limit has been met AND we were already jumping
			const bool bJumpKeyHeld = (bPressedJump && JumpKeyHoldTime < GetJumpMaxHoldTime());
			bCanJump = bJumpKeyHeld &&
					   (GetCharacterMovement()->IsMovingOnGround() || (JumpCurrentCount < JumpMaxCount) || (bWasJumping && JumpCurrentCount == JumpMaxCount));
		}
		if (GetCharacterMovement()->IsMovingOnGround())
		{
			const float FloorZ = FVector(0.0f, 0.0f, 1.0f) | GetCharacterMovement()->CurrentFloor.HitResult.ImpactNormal;
			const float WalkableFloor = GetCharacterMovement()->GetWalkableFloorZ();
			bCanJump &= (FloorZ >= WalkableFloor || FMath::IsNearlyEqual(FloorZ, WalkableFloor));
		}
	}

	return bCanJump;
}

// ReSharper disable once CppPassValueParameterByConstReference
void AEcoscapeFPPlayerCharacter::Move(FVector Direction, const float Value)
{
	if (!FMath::IsNearlyZero(Value))
	{
		// add movement in that direction
		AddMovementInput(Direction, Value);
	}
}

void AEcoscapeFPPlayerCharacter::Turn(const bool bIsPure, float Rate)
{
	if (!bIsPure)
	{
		Rate = Rate * BaseTurnRate * GetWorld()->GetDeltaSeconds();
	}

	// calculate delta for this frame from the rate information
	AddControllerYawInput(Rate);
}

void AEcoscapeFPPlayerCharacter::LookUp(const bool bIsPure, float Rate)
{
	if (!bIsPure)
	{
		Rate = Rate * BaseLookUpRate * GetWorld()->GetDeltaSeconds();
	}

	// calculate delta for this frame from the rate information
	AddControllerPitchInput(Rate);
}

void AEcoscapeFPPlayerCharacter::RecalculateBaseEyeHeight()
{
	const ACharacter* DefaultCharacter = GetClass()->GetDefaultObject<ACharacter>();
	const float OldUnscaledHalfHeight = DefaultCharacter->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
	const float CrouchedHalfHeight = GetCharacterMovement()->GetCrouchedHalfHeight();
	const float FullCrouchDiff = OldUnscaledHalfHeight - CrouchedHalfHeight;
	const UCapsuleComponent* CharacterCapsule = GetCapsuleComponent();
	const float CurrentUnscaledHalfHeight = CharacterCapsule->GetUnscaledCapsuleHalfHeight();
	const float CurrentAlpha = 1.0f - (CurrentUnscaledHalfHeight - CrouchedHalfHeight) / FullCrouchDiff;
	BaseEyeHeight = FMath::Lerp(DefaultCharacter->BaseEyeHeight, CrouchedEyeHeight, SimpleSpline(CurrentAlpha));
}

bool AEcoscapeFPPlayerCharacter::CanCrouch() const
{
	return !GetCharacterMovement()->bCheatFlying && Super::CanCrouch() && !MovementPtr->IsOnLadder();
}

void AEcoscapeFPPlayerCharacter::UnPossessed()
{
	HighlightObject(nullptr);
	bCurrentlyPossessed = false;
}
