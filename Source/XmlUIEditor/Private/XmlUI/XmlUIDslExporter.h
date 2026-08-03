#pragma once
#include "CoreMinimal.h"

/** Static editor tool that exports an existing Widget Blueprint back into the XmlUI DSL format. */
class FXmlUIDslExporter
{
public:
    static void RunExportFromDialog();
    static bool ExportWbpToDslFile(const FString& WbpAssetPath, const FString& OutXmlPath);
};
