// Copyright 2024/2025 Vladimir Alyamkin, Mauro Leoci. All Rights Reserved.

#pragma once

#include "Modules/ModuleInterface.h"

class FVaRestEditorModule : public IModuleInterface
{

public:
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
