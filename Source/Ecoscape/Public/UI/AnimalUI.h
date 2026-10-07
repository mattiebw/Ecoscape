// copyright lololol

#pragma once

#include "CoreMinimal.h"
#include "Character/Animals/BaseAnimal.h"
#include "Blueprint/UserWidget.h"
#include "AnimalUI.generated.h"

UINTERFACE(Blueprintable)
class UAnimalUI : public UInterface
{
	GENERATED_BODY()
};

class IAnimalUI
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, BlueprintImplementableEvent)
	void Show();
	UFUNCTION(BlueprintCallable, BlueprintImplementableEvent)
	void Hide();
	UFUNCTION(BlueprintCallable, BlueprintImplementableEvent)
	void SetAnimal(ABaseAnimal* Animal);
};

UCLASS(Blueprintable)
class UAnimalUnlockedWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintImplementableEvent)
	void SetAnimal(UAnimalData* Animal, const FString& HabitatName);
};
