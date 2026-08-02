#include "XmlUIEditor.h"

#include "XmlUI/XmlUIBaker.h"

#define LOCTEXT_NAMESPACE "FXmlUIEditorModule"

void FXmlUIEditorModule::StartupModule()
{
	FXmlUIBaker::RegisterMenus();
}

void FXmlUIEditorModule::ShutdownModule()
{
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FXmlUIEditorModule, XmlUIEditor)
