#pragma once

#include "Modules/ModuleManager.h"

class FXmlUIEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
