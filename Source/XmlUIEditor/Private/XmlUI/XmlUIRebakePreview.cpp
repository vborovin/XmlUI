#include "XmlUIBaker.h"
#include "XmlUIDslExporter.h"

#include "Editor.h"
#include "Blueprint/WidgetTree.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Misc/SecureHash.h"
#include "PackageTools.h"
#include "Subsystems/EditorAssetSubsystem.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "WidgetBlueprint.h"
#include "XmlDslNode.h"
#include "XmlDslParser.h"
#include "XmlDslValidator.h"

namespace
{
    struct FXmlRebakeDiff
    {
        TArray<FString> Messages;
        int32 Changed = 0;
        bool bStructureChanged = false;

        void Add(const FString& Message, bool bStructural)
        {
            Messages.Add(Message);
            ++Changed;
            bStructureChanged |= bStructural;
        }
    };

    FString DescribeValue(const FString& Value)
    {
        if (Value.Len() <= 160)
        {
            return FString::Printf(TEXT("'%s'"), *Value);
        }
        return FString::Printf(TEXT("'%s...' (length=%d, MD5=%s)"),
            *Value.Left(140), Value.Len(), *FMD5::HashAnsiString(*Value));
    }

    void DiffNodes(const FXmlNodeDesc& Before, const FXmlNodeDesc& After,
        const FString& Prefix, FXmlRebakeDiff& Diff)
    {
        const FString Path = Prefix + TEXT("/") + Before.Name;
        if (Before.Name != After.Name || Before.Tag != After.Tag)
        {
            Diff.Add(FString::Printf(TEXT("%s: widget changed <%s Name=%s> -> <%s Name=%s>"),
                *Path, *Before.Tag, *Before.Name, *After.Tag, *After.Name), true);
        }

        TArray<FString> Keys;
        Before.Attributes.GetKeys(Keys);
        Keys.Sort();
        for (const FString& Key : Keys)
        {
            const FString& OldValue = Before.Attributes.FindChecked(Key);
            const FString* NewValue = After.Attributes.Find(Key);
            if (!NewValue)
            {
                Diff.Add(FString::Printf(TEXT("%s.%s removed: %s"),
                    *Path, *Key, *DescribeValue(OldValue)), false);
            }
            else if (OldValue != *NewValue)
            {
                Diff.Add(FString::Printf(TEXT("%s.%s: %s -> %s"),
                    *Path, *Key, *DescribeValue(OldValue), *DescribeValue(*NewValue)),
                    Key == TEXT("ParentClass") || Key == TEXT("Class") || Key == TEXT("WBP"));
            }
        }

        TArray<FString> NewKeys;
        After.Attributes.GetKeys(NewKeys);
        NewKeys.Sort();
        for (const FString& Key : NewKeys)
        {
            if (!Before.Attributes.Contains(Key))
            {
                Diff.Add(FString::Printf(TEXT("%s.%s added: %s"),
                    *Path, *Key, *DescribeValue(After.Attributes.FindChecked(Key))),
                    Key == TEXT("Class") || Key == TEXT("WBP"));
            }
        }

        const int32 CommonCount = FMath::Min(Before.Children.Num(), After.Children.Num());
        for (int32 Index = 0; Index < CommonCount; ++Index)
        {
            DiffNodes(Before.Children[Index], After.Children[Index], Path, Diff);
        }
        for (int32 Index = CommonCount; Index < Before.Children.Num(); ++Index)
        {
            Diff.Add(FString::Printf(TEXT("%s: removed child <%s Name=%s>"),
                *Path, *Before.Children[Index].Tag, *Before.Children[Index].Name), true);
        }
        for (int32 Index = CommonCount; Index < After.Children.Num(); ++Index)
        {
            Diff.Add(FString::Printf(TEXT("%s: added child <%s Name=%s>"),
                *Path, *After.Children[Index].Tag, *After.Children[Index].Name), true);
        }
    }

    bool ParseFile(const FString& File, FXmlNodeDesc& Out, FString& Error)
    {
        FString Content;
        if (!FFileHelper::LoadFileToString(Content, *File))
        {
            Error = FString::Printf(TEXT("could not read XML file '%s'"), *File);
            return false;
        }
        return UXmlDslParser::ParseXmlString(Content, Out, Error);
    }

    struct FScopedPreviewArtifacts
    {
        TArray<FString> XmlFiles;
        TArray<FString> AssetPaths;

        ~FScopedPreviewArtifacts()
        {
            for (const FString& File : XmlFiles)
            {
                IFileManager::Get().Delete(*File);
            }
            for (const FString& AssetPath : AssetPaths)
            {
                if (!FindPackage(nullptr, *AssetPath) && !FPackageName::DoesPackageExist(AssetPath))
                {
                    continue;
                }
                UEditorAssetSubsystem* Assets = GEditor
                    ? GEditor->GetEditorSubsystem<UEditorAssetSubsystem>() : nullptr;
                if (!Assets || !Assets->DeleteAsset(AssetPath))
                {
                    UE_LOG(LogTemp, Error, TEXT("XmlUI Re-bake Preview: cleanup failed for %s"), *AssetPath);
                }
            }
        }
    };

    bool BakeAndReadCanonical(const FString& SourceXml, const FString& AssetPath,
        const FString& CanonicalXml, FScopedPreviewArtifacts& Artifacts,
        FXmlNodeDesc& OutNode, FString& Error)
    {
        if (FPackageName::DoesPackageExist(AssetPath))
        {
            Error = FString::Printf(TEXT("temporary asset already exists: %s"), *AssetPath);
            return false;
        }
        Artifacts.AssetPaths.Add(AssetPath);
        UWidgetBlueprint* NewBP = FXmlUIBaker::BakeDslToWidgetBlueprint(SourceXml, AssetPath, Error);
        if (!NewBP)
        {
            return false;
        }

        FText ReloadError;
        if (!UPackageTools::ReloadPackages({ NewBP->GetOutermost() }, ReloadError, false))
        {
            Error = FString::Printf(TEXT("temporary WBP reload failed: %s"), *ReloadError.ToString());
            return false;
        }
        if (!FXmlUIDslExporter::ExportWbpToDslFile(AssetPath, CanonicalXml))
        {
            Error = FString::Printf(TEXT("failed to re-export staged WBP %s"), *AssetPath);
            return false;
        }
        return ParseFile(CanonicalXml, OutNode, Error);
    }

    void PrintDiff(const TCHAR* Kind, const FXmlRebakeDiff& Diff)
    {
        // Avoid truncating the report silently on large Widgets: all differences
        // are logged, with long values shortened and accompanied by an MD5.
        for (const FString& Message : Diff.Messages)
        {
            UE_LOG(LogTemp, Warning, TEXT("XmlUI Re-bake %s: %s"), Kind, *Message);
        }
        UE_LOG(LogTemp, Display, TEXT("XmlUI Re-bake %s: %d changes, structureChanged=%s"),
            Kind, Diff.Changed, Diff.bStructureChanged ? TEXT("yes") : TEXT("no"));
    }

    bool Preview(const FString& WbpPath, const FString& XmlPath)
    {
        if (GEditor && GEditor->PlayWorld)
        {
            UE_LOG(LogTemp, Error, TEXT("XmlUI Re-bake Preview: stop PIE first"));
            return false;
        }
        if (!WbpPath.StartsWith(TEXT("/Game/")) || XmlPath.IsEmpty())
        {
            UE_LOG(LogTemp, Error, TEXT("XmlUI Re-bake Preview: expected Wbp=/Game/... File=/path/to/file.xml"));
            return false;
        }

        UWidgetBlueprint* Original = LoadObject<UWidgetBlueprint>(nullptr, *WbpPath);
        if (!Original || !Original->WidgetTree || !Original->WidgetTree->RootWidget)
        {
            UE_LOG(LogTemp, Error, TEXT("XmlUI Re-bake Preview: source WBP is missing or empty: %s"), *WbpPath);
            return false;
        }
        if (Original->GetOutermost()->IsDirty())
        {
            UE_LOG(LogTemp, Error, TEXT("XmlUI Re-bake Preview: save the source WBP first: %s"), *WbpPath);
            return false;
        }

        FXmlNodeDesc Candidate;
        FString Error;
        if (!ParseFile(XmlPath, Candidate, Error) || !FXmlDslValidator::ValidateDocument(Candidate, Error))
        {
            UE_LOG(LogTemp, Error, TEXT("XmlUI Re-bake Preview: invalid XML: %s"), *Error);
            return false;
        }
        const FString* ParentClass = Candidate.Attributes.Find(TEXT("ParentClass"));
        if (!ParentClass || !Original->ParentClass || *ParentClass != Original->ParentClass->GetPathName())
        {
            UE_LOG(LogTemp, Error,
                TEXT("XmlUI Re-bake Preview: ParentClass differs from current WBP; changing native parent is forbidden"));
            return false;
        }

        const FString Token = FGuid::NewGuid().ToString(EGuidFormats::Digits);
        const FString SavedDir = FPaths::ProjectSavedDir();
        const FString TempPrefix = TEXT("/Game/__XmlUIRebakePreview/WBP_XmlUIRebake_") + Token;
        FScopedPreviewArtifacts Artifacts;
        const FString CurrentXml = FPaths::Combine(SavedDir, TEXT("XmlUIRebake_") + Token + TEXT("_current.xml"));
        const FString CandidateXml = FPaths::Combine(SavedDir, TEXT("XmlUIRebake_") + Token + TEXT("_candidate.xml"));
        Artifacts.XmlFiles.Add(CurrentXml);
        Artifacts.XmlFiles.Add(CandidateXml);

        if (!FXmlUIDslExporter::ExportWbpToDslFile(WbpPath, CurrentXml))
        {
            UE_LOG(LogTemp, Error, TEXT("XmlUI Re-bake Preview: source WBP export failed"));
            return false;
        }
        FXmlNodeDesc CurrentNode;
        if (!ParseFile(CurrentXml, CurrentNode, Error))
        {
            UE_LOG(LogTemp, Error, TEXT("XmlUI Re-bake Preview: source exported XML is invalid: %s"), *Error);
            return false;
        }

        FXmlNodeDesc CanonicalCandidate;
        if (!BakeAndReadCanonical(XmlPath, TempPrefix + TEXT("_Candidate"),
            CandidateXml, Artifacts, CanonicalCandidate, Error))
        {
            UE_LOG(LogTemp, Error, TEXT("XmlUI Re-bake Preview: candidate bake/reload failed: %s"), *Error);
            return false;
        }

        FXmlRebakeDiff DesiredDiff;
        DiffNodes(CurrentNode, CanonicalCandidate, FString(), DesiredDiff);
        PrintDiff(TEXT("CHANGE"), DesiredDiff);

        UPackage* OriginalPackage = Original->GetOutermost();
        FMetaData& Meta = OriginalPackage->GetMetaData();
        const FString BaselineContent = Meta.GetValue(Original, TEXT("XmlUI.SourceDsl"));
        const FString BaselineHash = Meta.GetValue(Original, TEXT("XmlUI.SourceHash"));

        if (BaselineContent.IsEmpty() || BaselineHash.IsEmpty()
            || FMD5::HashAnsiString(*BaselineContent) != BaselineHash)
        {
            UE_LOG(LogTemp, Warning,
                TEXT("XmlUI Re-bake Preview: UNMANAGED — WBP has no valid XmlUI source baseline; cannot verify manual changes"));
            UE_LOG(LogTemp, Display, TEXT("XmlUI Re-bake Preview: FINISHED (changes=%d, baseline=unknown, source unchanged)"),
                DesiredDiff.Changed);
            return true;
        }

        const FString BaselineFile = FPaths::Combine(SavedDir, TEXT("XmlUIRebake_") + Token + TEXT("_baseline.xml"));
        const FString BaselineExportFile = FPaths::Combine(SavedDir, TEXT("XmlUIRebake_") + Token + TEXT("_baseline_export.xml"));
        Artifacts.XmlFiles.Add(BaselineFile);
        Artifacts.XmlFiles.Add(BaselineExportFile);
        if (!FFileHelper::SaveStringToFile(BaselineContent, *BaselineFile))
        {
            UE_LOG(LogTemp, Error, TEXT("XmlUI Re-bake Preview: cannot write source baseline XML"));
            return false;
        }

        FXmlNodeDesc BaselineNode;
        if (!BakeAndReadCanonical(BaselineFile, TempPrefix + TEXT("_Baseline"),
            BaselineExportFile, Artifacts, BaselineNode, Error))
        {
            UE_LOG(LogTemp, Warning, TEXT("XmlUI Re-bake Preview: CONFLICT CHECK FAILED — baseline cannot be rebuilt: %s"),
                *Error);
            return false;
        }

        FXmlRebakeDiff Conflicts;
        DiffNodes(BaselineNode, CurrentNode, FString(), Conflicts);
        PrintDiff(TEXT("MANUAL-CONFLICT"), Conflicts);
        UE_LOG(LogTemp, Display, TEXT("XmlUI Re-bake Preview: %s — requestedChanges=%d, conflicts=%d, source unchanged"),
            Conflicts.Changed ? TEXT("CONFLICT") : TEXT("READY"),
            DesiredDiff.Changed, Conflicts.Changed);
        return Conflicts.Changed == 0;
    }
}

static FAutoConsoleCommand GXmlUIRebakePreviewCommand(
    TEXT("XmlUI.RebakePreview"),
    TEXT("Preview safe changes without modifying the WBP. Usage: XmlUI.RebakePreview Wbp=/Game/UI/WBP_Name File=/path/to/ui.xml"),
    FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
    {
        const FString CommandLine = FString::Join(Args, TEXT(" "));
        FString WbpPath, XmlPath;
        FParse::Value(*CommandLine, TEXT("Wbp="), WbpPath);
        FParse::Value(*CommandLine, TEXT("File="), XmlPath);
        Preview(WbpPath, XmlPath);
    }));

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FXmlUIRebakeDiffTest,
    "XmlUI.RebakePreview.Diff", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FXmlUIRebakeDiffTest::RunTest(const FString& Parameters)
{
    FXmlNodeDesc Before, After;
    FString Error;
    TestTrue(TEXT("parse before"), UXmlDslParser::ParseXmlString(
        TEXT("<Vertical Name=\"Root\"><Text Name=\"Title\" Text=\"Before\" FontSize=\"20\"/></Vertical>"),
        Before, Error));
    TestTrue(TEXT("parse after"), UXmlDslParser::ParseXmlString(
        TEXT("<Vertical Name=\"Root\"><Text Name=\"Title\" Text=\"After\" FontSize=\"20\"/></Vertical>"),
        After, Error));

    FXmlRebakeDiff Diff;
    DiffNodes(Before, After, FString(), Diff);
    TestEqual(TEXT("one changed property"), Diff.Changed, 1);
    TestFalse(TEXT("no structural change"), Diff.bStructureChanged);
    TestTrue(TEXT("reported path"), Diff.Messages.Num() == 1
        && Diff.Messages[0].Contains(TEXT("/Root/Title.Text")));

    After.Children[0].Name = TEXT("TitleRenamed");
    FXmlRebakeDiff Structural;
    DiffNodes(Before, After, FString(), Structural);
    TestTrue(TEXT("rename is structural"), Structural.bStructureChanged);
    return true;
}
#endif
