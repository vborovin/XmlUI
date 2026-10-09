#include "XmlUIDslExporter.h"
#include "XmlUIBaker.h"

#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Fonts/SlateFontInfo.h"
#include "Editor.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "PackageTools.h"
#include "Subsystems/EditorAssetSubsystem.h"
#include "UObject/Package.h"
#include "WidgetBlueprint.h"
#include "XmlDslNode.h"
#include "XmlDslParser.h"

namespace
{
    struct FXmlRoundTripResult
    {
        TArray<FString> Differences;
        int32 ComparedWidgets = 0;
        int32 ComparedAttributes = 0;
    };

    FString ShortValue(const FString& Value)
    {
        return Value.Len() > 180 ? Value.Left(177) + TEXT("...") : Value;
    }

    void CompareNodes(const FXmlNodeDesc& Source, const FXmlNodeDesc& Rebuilt,
        const FString& ParentPath, FXmlRoundTripResult& Result)
    {
        const FString WidgetPath = ParentPath + TEXT("/") + Source.Name;
        ++Result.ComparedWidgets;

        if (Source.Name != Rebuilt.Name || Source.Tag != Rebuilt.Tag)
        {
            Result.Differences.Add(FString::Printf(
                TEXT("%s: widget identity differs: <%s Name='%s'> vs <%s Name='%s'>"),
                *WidgetPath, *Source.Tag, *Source.Name, *Rebuilt.Tag, *Rebuilt.Name));
        }

        TArray<FString> SourceKeys;
        Source.Attributes.GetKeys(SourceKeys);
        SourceKeys.Sort();
        for (const FString& Key : SourceKeys)
        {
            ++Result.ComparedAttributes;
            const FString* OtherValue = Rebuilt.Attributes.Find(Key);
            if (!OtherValue)
            {
                Result.Differences.Add(FString::Printf(
                    TEXT("%s.%s: missing after round-trip (was '%s')"),
                    *WidgetPath, *Key, *ShortValue(Source.Attributes.FindChecked(Key))));
            }
            else if (*OtherValue != Source.Attributes.FindChecked(Key))
            {
                Result.Differences.Add(FString::Printf(
                    TEXT("%s.%s: before='%s' after='%s'"),
                    *WidgetPath, *Key,
                    *ShortValue(Source.Attributes.FindChecked(Key)), *ShortValue(*OtherValue)));
            }
        }

        TArray<FString> RebuiltKeys;
        Rebuilt.Attributes.GetKeys(RebuiltKeys);
        RebuiltKeys.Sort();
        for (const FString& Key : RebuiltKeys)
        {
            if (!Source.Attributes.Contains(Key))
            {
                Result.Differences.Add(FString::Printf(
                    TEXT("%s.%s: added after round-trip ('%s')"),
                    *WidgetPath, *Key, *ShortValue(Rebuilt.Attributes.FindChecked(Key))));
            }
        }

        if (Source.Children.Num() != Rebuilt.Children.Num())
        {
            Result.Differences.Add(FString::Printf(
                TEXT("%s: child count changed (%d -> %d)"),
                *WidgetPath, Source.Children.Num(), Rebuilt.Children.Num()));
        }

        const int32 SharedChildren = FMath::Min(Source.Children.Num(), Rebuilt.Children.Num());
        for (int32 Index = 0; Index < SharedChildren; ++Index)
        {
            CompareNodes(Source.Children[Index], Rebuilt.Children[Index], WidgetPath, Result);
        }
        for (int32 Index = SharedChildren; Index < Source.Children.Num(); ++Index)
        {
            Result.Differences.Add(FString::Printf(
                TEXT("%s: child '%s' removed"), *WidgetPath, *Source.Children[Index].Name));
        }
        for (int32 Index = SharedChildren; Index < Rebuilt.Children.Num(); ++Index)
        {
            Result.Differences.Add(FString::Printf(
                TEXT("%s: child '%s' added"), *WidgetPath, *Rebuilt.Children[Index].Name));
        }
    }

    struct FScopedValidationArtifacts
    {
        FString SourceXmlFile;
        FString RebuiltXmlFile;
        FString TempAssetPath;
        bool bMayHaveCreatedAsset = false;

        ~FScopedValidationArtifacts()
        {
            if (!SourceXmlFile.IsEmpty())
            {
                IFileManager::Get().Delete(*SourceXmlFile);
            }
            if (!RebuiltXmlFile.IsEmpty())
            {
                IFileManager::Get().Delete(*RebuiltXmlFile);
            }
            if (bMayHaveCreatedAsset && !TempAssetPath.IsEmpty()
                && (FindPackage(nullptr, *TempAssetPath) || FPackageName::DoesPackageExist(TempAssetPath)))
            {
                UEditorAssetSubsystem* Assets = GEditor
                    ? GEditor->GetEditorSubsystem<UEditorAssetSubsystem>() : nullptr;
                if (!Assets || !Assets->DeleteAsset(TempAssetPath))
                {
                    UE_LOG(LogTemp, Error,
                        TEXT("XmlUI RoundTrip: could not remove temporary asset '%s'; remove it manually"),
                        *TempAssetPath);
                }
            }
        }
    };

    bool ValidateRoundTrip(const FString& WbpPath)
    {
        if (!WbpPath.StartsWith(TEXT("/Game/")))
        {
            UE_LOG(LogTemp, Error, TEXT("XmlUI RoundTrip: Wbp must be a /Game/ asset path"));
            return false;
        }

        UWidgetBlueprint* Original = LoadObject<UWidgetBlueprint>(nullptr, *WbpPath);
        if (!Original || !Original->WidgetTree || !Original->WidgetTree->RootWidget)
        {
            UE_LOG(LogTemp, Error, TEXT("XmlUI RoundTrip: WBP not found or its WidgetTree is empty: %s"), *WbpPath);
            return false;
        }
        if (Original->GetPackage()->IsDirty())
        {
            UE_LOG(LogTemp, Error, TEXT("XmlUI RoundTrip: save '%s' before validating its persisted state"), *WbpPath);
            return false;
        }

        const FString Token = FGuid::NewGuid().ToString(EGuidFormats::Digits);
        FScopedValidationArtifacts Temp;
        Temp.SourceXmlFile = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("XmlUIValidation_") + Token + TEXT("_source.xml"));
        Temp.RebuiltXmlFile = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("XmlUIValidation_") + Token + TEXT("_rebuilt.xml"));
        Temp.TempAssetPath = TEXT("/Game/__XmlUIValidation/WBP_XmlUIValidation_") + Token;
        const FString TempObjectPath = Temp.TempAssetPath + TEXT(".WBP_XmlUIValidation_") + Token;

        if (FPackageName::DoesPackageExist(Temp.TempAssetPath))
        {
            UE_LOG(LogTemp, Error, TEXT("XmlUI RoundTrip: temporary asset path already exists"));
            return false;
        }

        UE_LOG(LogTemp, Display, TEXT("XmlUI RoundTrip: validating %s"), *WbpPath);
        if (!FXmlUIDslExporter::ExportWbpToDslFile(WbpPath, Temp.SourceXmlFile))
        {
            UE_LOG(LogTemp, Error, TEXT("XmlUI RoundTrip: source export failed"));
            return false;
        }

        FString Error;
        Temp.bMayHaveCreatedAsset = true;
        UWidgetBlueprint* Baked = FXmlUIBaker::BakeDslToWidgetBlueprint(
            Temp.SourceXmlFile, Temp.TempAssetPath, Error);
        if (!Baked)
        {
            UE_LOG(LogTemp, Error, TEXT("XmlUI RoundTrip: bake or compile failed: %s"), *Error);
            return false;
        }

        // Reload only our GUID-named temporary package to verify its on-disk representation.
        UPackage* TempPackage = Baked->GetOutermost();
        FText ReloadError;
        if (!UPackageTools::ReloadPackages({ TempPackage }, ReloadError, false))
        {
            UE_LOG(LogTemp, Error, TEXT("XmlUI RoundTrip: saved package reload failed: %s"),
                *ReloadError.ToString());
            return false;
        }

        UWidgetBlueprint* Reloaded = LoadObject<UWidgetBlueprint>(nullptr, *TempObjectPath);
        const UWidgetBlueprintGeneratedClass* Generated = Reloaded
            ? Cast<UWidgetBlueprintGeneratedClass>(Reloaded->GeneratedClass) : nullptr;
        if (!Reloaded || !Reloaded->WidgetTree || !Reloaded->WidgetTree->RootWidget
            || !Generated || !Generated->GetWidgetTreeArchetype())
        {
            UE_LOG(LogTemp, Error, TEXT("XmlUI RoundTrip: reloaded Blueprint has no editable or generated tree"));
            return false;
        }

        if (!FXmlUIDslExporter::ExportWbpToDslFile(Temp.TempAssetPath, Temp.RebuiltXmlFile))
        {
            UE_LOG(LogTemp, Error, TEXT("XmlUI RoundTrip: reloaded asset export failed"));
            return false;
        }

        FString SourceContent, RebuiltContent;
        if (!FFileHelper::LoadFileToString(SourceContent, *Temp.SourceXmlFile)
            || !FFileHelper::LoadFileToString(RebuiltContent, *Temp.RebuiltXmlFile))
        {
            UE_LOG(LogTemp, Error, TEXT("XmlUI RoundTrip: could not read exported DSL files"));
            return false;
        }

        FXmlNodeDesc SourceNode, RebuiltNode;
        FString SourceError, RebuiltError;
        if (!UXmlDslParser::ParseXmlString(SourceContent, SourceNode, SourceError)
            || !UXmlDslParser::ParseXmlString(RebuiltContent, RebuiltNode, RebuiltError))
        {
            UE_LOG(LogTemp, Error, TEXT("XmlUI RoundTrip: invalid exported DSL: %s %s"),
                *SourceError, *RebuiltError);
            return false;
        }

        FXmlRoundTripResult Result;
        CompareNodes(SourceNode, RebuiltNode, FString(), Result);

        // The exporter inspects the editable tree. Also verify the persisted
        // compiled template to detect CommonUI font migration regressions.
        const UWidgetTree* CompiledTree = Generated->GetWidgetTreeArchetype();
        TArray<UWidget*> EditableWidgets;
        Reloaded->WidgetTree->GetAllWidgets(EditableWidgets);
        for (const UWidget* Widget : EditableWidgets)
        {
            const UTextBlock* EditableText = Cast<UTextBlock>(Widget);
            if (!EditableText)
            {
                continue;
            }
            const UTextBlock* CompiledText = CompiledTree->FindWidget<UTextBlock>(EditableText->GetFName());
            if (!CompiledText)
            {
                Result.Differences.Add(FString::Printf(TEXT("/%s: missing in reloaded GeneratedTree"),
                    *EditableText->GetName()));
                continue;
            }
            const FSlateFontInfo EditableFont = EditableText->GetFont();
            const FSlateFontInfo CompiledFont = CompiledText->GetFont();
            if (EditableFont.FontObject != CompiledFont.FontObject
                || EditableFont.TypefaceFontName != CompiledFont.TypefaceFontName
                || EditableFont.Size != CompiledFont.Size)
            {
                Result.Differences.Add(FString::Printf(
                    TEXT("/%s.Font: reloaded editable='%s/%s/%.2f' generated='%s/%s/%.2f'"),
                    *EditableText->GetName(),
                    *GetPathNameSafe(EditableFont.FontObject), *EditableFont.TypefaceFontName.ToString(),
                    static_cast<double>(EditableFont.Size),
                    *GetPathNameSafe(CompiledFont.FontObject), *CompiledFont.TypefaceFontName.ToString(),
                    static_cast<double>(CompiledFont.Size)));
            }
        }

        for (const FString& Difference : Result.Differences)
        {
            UE_LOG(LogTemp, Warning, TEXT("XmlUI RoundTrip DIFF: %s"), *Difference);
        }
        UE_LOG(LogTemp, Display, TEXT("XmlUI RoundTrip: %s — %d widgets, %d attributes, %d differences (export -> bake -> compile -> save -> reload -> export)"),
            Result.Differences.IsEmpty() ? TEXT("PASS") : TEXT("FAIL"),
            Result.ComparedWidgets, Result.ComparedAttributes, Result.Differences.Num());
        return Result.Differences.IsEmpty();
    }
}

static FAutoConsoleCommand GXmlUIValidateRoundTripCommand(
    TEXT("XmlUI.ValidateRoundTrip"),
    TEXT("Validate an existing WBP without modifying it. Usage: XmlUI.ValidateRoundTrip Wbp=/Game/UI/WBP_Name"),
    FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
    {
        FString WbpPath;
        FParse::Value(*FString::Join(Args, TEXT(" ")), TEXT("Wbp="), WbpPath);
        if (WbpPath.IsEmpty())
        {
            UE_LOG(LogTemp, Error, TEXT("XmlUI.ValidateRoundTrip: expected Wbp=/Game/..."));
            return;
        }
        ValidateRoundTrip(WbpPath);
    }));

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FXmlUIRoundTripNodeCompareTest,
    "XmlUI.RoundTrip.NodeCompare", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FXmlUIRoundTripNodeCompareTest::RunTest(const FString& Parameters)
{
    FXmlNodeDesc Source, Target;
    FString Error;
    const FString Xml = TEXT("<Horizontal Name=\"Root\"><Text Name=\"Caption\" Text=\"Hi\" FontSize=\"20\"/></Horizontal>");
    TestTrue(TEXT("parse source"), UXmlDslParser::ParseXmlString(Xml, Source, Error));
    TestTrue(TEXT("parse target"), UXmlDslParser::ParseXmlString(Xml, Target, Error));

    FXmlRoundTripResult Same;
    CompareNodes(Source, Target, FString(), Same);
    TestEqual(TEXT("two widgets"), Same.ComparedWidgets, 2);
    TestTrue(TEXT("identical trees compare equal"), Same.Differences.IsEmpty());

    Target.Children[0].Attributes.Add(TEXT("FontSize"), TEXT("24"));
    FXmlRoundTripResult Changed;
    CompareNodes(Source, Target, FString(), Changed);
    TestEqual(TEXT("font property mismatch reported"), Changed.Differences.Num(), 1);
    TestTrue(TEXT("report identifies widget and property"),
        Changed.Differences.Num() == 1
        && Changed.Differences[0].Contains(TEXT("/Root/Caption.FontSize")));

    Target.Children.Empty();
    FXmlRoundTripResult Missing;
    CompareNodes(Source, Target, FString(), Missing);
    TestTrue(TEXT("removed child reported"), Missing.Differences.Num() > 0);
    return true;
}

#endif
