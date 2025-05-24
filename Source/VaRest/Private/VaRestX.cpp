// Copyright 2024/2025 Vladimir Alyamkin, Mauro Leoci. All Rights Reserved.

#include "VaRestX.h"

#include "VaRestDefines.h"
#include "VaRestLibrary.h"
#include "VaRestSettings.h"

#include "Developer/Settings/Public/ISettingsModule.h"
#include "UObject/Package.h"

#define LOCTEXT_NAMESPACE "FVaRestModule"

void FVaRestModule::StartupModule()
{
	ModuleSettings = NewObject<UVaRestSettings>(GetTransientPackage(), "VaRestSettings", RF_Standalone);
	ModuleSettings->AddToRoot();

	// Register settings
	if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
	{
		SettingsModule->RegisterSettings("Project", "Plugins", "VaRestX",
			LOCTEXT("RuntimeSettingsName", "VaRestX"),
			LOCTEXT("RuntimeSettingsDescription", "Configure VaRestX plugin settings"),
			ModuleSettings);
	}
	else
	{
		UE_LOG(LogVaRest, Warning, TEXT("Settings module is unavailable. Skipping settings registration."));
	}

	UE_LOG(LogVaRest, Log, TEXT("%s: VaRestX (%s) module started"), *VA_FUNC_LINE, *UVaRestLibrary::GetVaRestVersion());
}

void FVaRestModule::ShutdownModule()
{
	if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
	{
		SettingsModule->UnregisterSettings("Project", "Plugins", "VaRestX");
	}

	if (!GExitPurge)
	{
		ModuleSettings->RemoveFromRoot();
	}
	else
	{
		ModuleSettings = nullptr;
	}
}

UVaRestSettings* FVaRestModule::GetSettings() const
{
	check(ModuleSettings);
	return ModuleSettings;
}

IMPLEMENT_MODULE(FVaRestModule, VaRest)

DEFINE_LOG_CATEGORY(LogVaRest);

#undef LOCTEXT_NAMESPACE
