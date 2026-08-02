#pragma once

#include "CoreMinimal.h"
#include "XmlDslNode.generated.h"

USTRUCT(BlueprintType)
struct XMLUI_API FXmlNodeDesc
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly)
    FString Tag;

    UPROPERTY(BlueprintReadOnly)
    FString Name;

    UPROPERTY(BlueprintReadOnly)
    TMap<FString, FString> Attributes;

    // UHT does not support recursive array reflection, so Children is not a UPROPERTY
    TArray<FXmlNodeDesc> Children;
};
