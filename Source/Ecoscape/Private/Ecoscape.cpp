// copyright lololol

#include "Ecoscape.h"

#include "EcoscapeLog.h"
#include "Modules/ModuleManager.h"

#include "MessageLogInitializationOptions.h"
#include "MessageLogModule.h"

DEFINE_LOG_CATEGORY(LogEcoscape)

IMPLEMENT_PRIMARY_GAME_MODULE( FEcoscapeModule, Ecoscape, "Ecoscape" );

#define LOCTEXT_NAMESPACE "Ecoscape"

void FEcoscapeModule::StartupModule()
{
#if WITH_EDITOR
	FMessageLogModule& MessageLogModule = FModuleManager::LoadModuleChecked<FMessageLogModule>("MessageLog");
	FMessageLogInitializationOptions InitOptions;
	InitOptions.bShowPages = true;
	InitOptions.bAllowClear = true;
	InitOptions.bShowFilters = true;
	MessageLogModule.RegisterLogListing("Ecoscape", NSLOCTEXT("Ecoscape", "EcoscapeLogLabel", "Ecoscape"), InitOptions);
#endif
	
	UE_LOG(LogEcoscape, Log, TEXT("Started main Ecoscape module!"));
}

void FEcoscapeModule::ShutdownModule()
{
#if WITH_EDITOR
	if (FModuleManager::Get().IsModuleLoaded("MessageLog"))
	{
		// Unregister message log
		FMessageLogModule& MessageLogModule = FModuleManager::GetModuleChecked<FMessageLogModule>("MessageLog");
		MessageLogModule.UnregisterLogListing("Ecoscape");
	}
#endif
	
	UE_LOG(LogEcoscape, Log, TEXT("Shutdown main Ecoscape module!"));
}

#undef LOCTEXT_NAMESPACE
