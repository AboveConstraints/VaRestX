// Copyright 2024/2025 Vladimir Alyamkin, Mauro Leoci. All Rights Reserved.

#include "VaRestSettings.h"

UVaRestSettings::UVaRestSettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bExtendedLog = false;
	bUseChunkedParser = false;
}
