#pragma once

#include "CoreMinimal.h"
#include "XmlDslNode.h"

/**
 * Validates the declarative XmlUI AST before it reaches the widget builder.
 *
 * The builder historically treats unknown tags/attributes as best-effort input.
 * Strict validation makes the DSL deterministic for generated/source-controlled UI:
 * a typo fails the build instead of silently producing a partial widget tree.
 */
class XMLUI_API FXmlDslValidator
{
public:
    static bool ValidateDocument(const FXmlNodeDesc& Root, FString& OutError);
};
