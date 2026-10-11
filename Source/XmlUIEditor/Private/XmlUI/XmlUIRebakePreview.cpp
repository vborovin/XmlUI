#include "XmlUIBaker.h"
#include "XmlUIDslExporter.h"
#include "XmlVisualStyle.h"

#include "Editor.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Components/TextBlock.h"
#include "FileHelpers.h"
#include "Fonts/SlateFontInfo.h"
#include "HAL/PlatformFileManager.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "ScopedTransaction.h"
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
#include "UObject/UnrealType.h"
#include "UObject/StrongObjectPtr.h"
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

    bool IsAllowedChange(const FString& Tag, const FString& Key)
    {
        if (Key.StartsWith(TEXT("Visual.")))
        {
            return FXmlVisualStyle::IsSupported(Tag, Key.RightChop(7));
        }
        if (Key == TEXT("Text") || Key == TEXT("FontSize")
            || Key == TEXT("FontPath") || Key == TEXT("Typeface") || Key == TEXT("Color"))
        {
            return Tag == TEXT("Text");
        }
        return Key == TEXT("Visibility") || Key == TEXT("RenderOpacity");
    }

    // Run the exact same traversal for preflight, apply and rollback.
    // Only existing widget properties are touched; no widget is replaced.
    bool CopyChangedProperties(const FXmlNodeDesc& Before, const FXmlNodeDesc& After,
        UWidgetTree* DestinationTree, const UWidgetTree* SourceTree,
        bool bDryRun, FString& Error)
    {
        if (Before.Name != After.Name || Before.Tag != After.Tag
            || Before.Children.Num() != After.Children.Num())
        {
            Error = FString::Printf(TEXT("structure changed at %s; in-place Apply is disabled"),
                *Before.Name);
            return false;
        }

        UWidget* Target = DestinationTree->FindWidget(FName(*Before.Name));
        const UWidget* Source = SourceTree->FindWidget(FName(*Before.Name));
        if (!Target || !Source || Target->GetClass() != Source->GetClass())
        {
            Error = FString::Printf(TEXT("%s: staged widget is missing or has a different class"), *Before.Name);
            return false;
        }

        TSet<FString> Keys;
        for (const TPair<FString, FString>& Item : Before.Attributes) Keys.Add(Item.Key);
        for (const TPair<FString, FString>& Item : After.Attributes) Keys.Add(Item.Key);

        TArray<FString> SortedKeys = Keys.Array();
        SortedKeys.Sort();
        for (const FString& Key : SortedKeys)
        {
            const FString* OldValue = Before.Attributes.Find(Key);
            const FString* NewValue = After.Attributes.Find(Key);
            if (OldValue && NewValue && *OldValue == *NewValue)
            {
                continue;
            }
            if (!IsAllowedChange(Before.Tag, Key))
            {
                Error = FString::Printf(TEXT("%s.%s: attribute is not supported by property-only Apply"),
                    *Before.Name, *Key);
                return false;
            }

            const UTextBlock* SourceText = Cast<UTextBlock>(Source);
            UTextBlock* TargetText = Cast<UTextBlock>(Target);
            const bool bFontAlias = Key == TEXT("FontSize") || Key == TEXT("FontPath")
                || Key == TEXT("Typeface");
            if (Key == TEXT("Text"))
            {
                if (!SourceText || !TargetText)
                {
                    Error = FString::Printf(TEXT("%s.Text: only UTextBlock is supported"), *Before.Name);
                    return false;
                }
                if (!bDryRun) TargetText->SetText(SourceText->GetText());
            }
            else if (bFontAlias)
            {
                if (!SourceText || !TargetText)
                {
                    Error = FString::Printf(TEXT("%s.%s: only UTextBlock fonts are supported"), *Before.Name, *Key);
                    return false;
                }
                if (!bDryRun) TargetText->SetFont(SourceText->GetFont());
            }
            else if (Key == TEXT("Color"))
            {
                if (!SourceText || !TargetText)
                {
                    Error = FString::Printf(TEXT("%s.Color: only UTextBlock is supported"), *Before.Name);
                    return false;
                }
                if (!bDryRun) TargetText->SetColorAndOpacity(SourceText->GetColorAndOpacity());
            }
            else if (Key == TEXT("Visibility"))
            {
                if (!bDryRun) Target->SetVisibility(Source->GetVisibility());
            }
            else if (Key == TEXT("RenderOpacity"))
            {
                if (!bDryRun) Target->SetRenderOpacity(Source->GetRenderOpacity());
            }
            else if (Key.StartsWith(TEXT("Visual.")))
            {
                const FString PropertyName = Key.RightChop(7);
                FProperty* Property = FindFProperty<FProperty>(Target->GetClass(), FName(*PropertyName));
                const FProperty* SourceProperty = FindFProperty<FProperty>(Source->GetClass(), FName(*PropertyName));
                if (!Property || !SourceProperty || !Property->SameType(SourceProperty)
                    || !Property->HasAnyPropertyFlags(CPF_Edit)
                    || Property->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated))
                {
                    Error = FString::Printf(TEXT("%s.%s: reflected property is incompatible"),
                        *Before.Name, *Key);
                    return false;
                }
                if (!bDryRun)
                {
                    Property->CopyCompleteValue_InContainer(Target, Source);
                    if (Key == TEXT("Visual.Font"))
                    {
                        if (UTextBlock* Text = Cast<UTextBlock>(Target))
                        {
                            Text->SetFont(Text->GetFont());
                        }
                    }
                    // XmlUI legacy widgets have companion flags controlling
                    // whether their native visual property is actually used.
                    if (Key == TEXT("Visual.Font") || Key == TEXT("Visual.WidgetStyle"))
                    {
                        const TCHAR* FlagName = Key == TEXT("Visual.Font")
                            ? TEXT("bUseVisualFont") : TEXT("bUseVisualWidgetStyle");
                        FBoolProperty* DestFlag = FindFProperty<FBoolProperty>(Target->GetClass(), FlagName);
                        const FBoolProperty* SrcFlag = FindFProperty<FBoolProperty>(Source->GetClass(), FlagName);
                        if (DestFlag && SrcFlag)
                        {
                            DestFlag->SetPropertyValue_InContainer(Target,
                                SrcFlag->GetPropertyValue_InContainer(Source));
                        }
                    }
                }
            }
        }

        for (int32 Index = 0; Index < Before.Children.Num(); ++Index)
        {
            if (!CopyChangedProperties(Before.Children[Index], After.Children[Index],
                DestinationTree, SourceTree, bDryRun, Error))
            {
                return false;
            }
        }
        return true;
    }

    bool VerifyCompiledChanges(const FXmlNodeDesc& Before, const FXmlNodeDesc& After,
        const UWidgetBlueprint* Actual, const UWidgetBlueprint* Staged, FString& Error)
    {
        const UWidgetBlueprintGeneratedClass* ActualClass = Actual
            ? Cast<UWidgetBlueprintGeneratedClass>(Actual->GeneratedClass) : nullptr;
        const UWidgetBlueprintGeneratedClass* StageClass = Staged
            ? Cast<UWidgetBlueprintGeneratedClass>(Staged->GeneratedClass) : nullptr;
        const UWidgetTree* ActualTree = ActualClass ? ActualClass->GetWidgetTreeArchetype() : nullptr;
        const UWidgetTree* StageTree = StageClass ? StageClass->GetWidgetTreeArchetype() : nullptr;
        if (!ActualTree || !StageTree)
        {
            Error = TEXT("generated WidgetTree unavailable for property verification");
            return false;
        }

        const UWidget* ActualWidget = ActualTree->FindWidget(FName(*Before.Name));
        const UWidget* StageWidget = StageTree->FindWidget(FName(*Before.Name));
        if (!ActualWidget || !StageWidget || ActualWidget->GetClass() != StageWidget->GetClass())
        {
            Error = FString::Printf(TEXT("%s: generated widget class differs"), *Before.Name);
            return false;
        }

        TSet<FString> Keys;
        for (const TPair<FString, FString>& Item : Before.Attributes) Keys.Add(Item.Key);
        for (const TPair<FString, FString>& Item : After.Attributes) Keys.Add(Item.Key);
        for (const FString& Key : Keys)
        {
            const FString* OldValue = Before.Attributes.Find(Key);
            const FString* NewValue = After.Attributes.Find(Key);
            if (OldValue && NewValue && *OldValue == *NewValue) continue;

            bool bMatches = false;
            const UTextBlock* ActualText = Cast<UTextBlock>(ActualWidget);
            const UTextBlock* StageText = Cast<UTextBlock>(StageWidget);
            if (Key == TEXT("Text") && ActualText && StageText)
            {
                bMatches = ActualText->GetText().ToString() == StageText->GetText().ToString();
            }
            else if ((Key == TEXT("FontSize") || Key == TEXT("FontPath")
                || Key == TEXT("Typeface")) && ActualText && StageText)
            {
                const FSlateFontInfo A = ActualText->GetFont();
                const FSlateFontInfo B = StageText->GetFont();
                bMatches = A.FontObject == B.FontObject
                    && A.TypefaceFontName == B.TypefaceFontName && A.Size == B.Size;
            }
            else if (Key == TEXT("Visibility"))
            {
                bMatches = ActualWidget->GetVisibility() == StageWidget->GetVisibility();
            }
            else if (Key == TEXT("RenderOpacity"))
            {
                bMatches = FMath::IsNearlyEqual(ActualWidget->GetRenderOpacity(),
                    StageWidget->GetRenderOpacity());
            }
            else
            {
                const FString PropertyName = Key.StartsWith(TEXT("Visual."))
                    ? Key.RightChop(7)
                    : Key == TEXT("Color") ? TEXT("ColorAndOpacity") : FString();
                if (!PropertyName.IsEmpty())
                {
                    const FProperty* P = FindFProperty<FProperty>(ActualWidget->GetClass(), FName(*PropertyName));
                    bMatches = P && P->Identical_InContainer(ActualWidget, StageWidget);
                }
            }
            if (!bMatches)
            {
                Error = FString::Printf(TEXT("%s.%s: generated value differs from staged WBP"),
                    *Before.Name, *Key);
                return false;
            }
        }
        for (int32 Index = 0; Index < Before.Children.Num(); ++Index)
        {
            if (!VerifyCompiledChanges(Before.Children[Index], After.Children[Index],
                Actual, Staged, Error))
            {
                return false;
            }
        }
        return true;
    }

    bool VerifyTextTemplates(const UWidgetBlueprint* BP, FString& Error)
    {
        const UWidgetBlueprintGeneratedClass* Class = BP
            ? Cast<UWidgetBlueprintGeneratedClass>(BP->GeneratedClass) : nullptr;
        const UWidgetTree* TemplateTree = Class ? Class->GetWidgetTreeArchetype() : nullptr;
        if (!BP || !BP->WidgetTree || !TemplateTree)
        {
            Error = TEXT("compiled WidgetTree is missing");
            return false;
        }
        TArray<UWidget*> All;
        BP->WidgetTree->GetAllWidgets(All);
        for (const UWidget* Item : All)
        {
            const UTextBlock* Text = Cast<UTextBlock>(Item);
            if (!Text) continue;
            const UTextBlock* Compiled = TemplateTree->FindWidget<UTextBlock>(Text->GetFName());
            if (!Compiled
                || Text->GetFont().FontObject != Compiled->GetFont().FontObject
                || Text->GetFont().TypefaceFontName != Compiled->GetFont().TypefaceFontName
                || Text->GetFont().Size != Compiled->GetFont().Size)
            {
                Error = FString::Printf(TEXT("generated font mismatch on widget %s"), *Text->GetName());
                return false;
            }
        }
        return true;
    }

    bool Apply(const FString& WbpPath, const FString& XmlPath, const FString& Confirmation)
    {
        if (Confirmation != TEXT("YES"))
        {
            UE_LOG(LogTemp, Error, TEXT("XmlUI Re-bake Apply: explicit Confirm=YES is required"));
            return false;
        }
        if (!Preview(WbpPath, XmlPath))
        {
            UE_LOG(LogTemp, Error, TEXT("XmlUI Re-bake Apply: preview failed or reported conflicts; no changes applied"));
            return false;
        }

        UWidgetBlueprint* Original = LoadObject<UWidgetBlueprint>(nullptr, *WbpPath);
        if (!Original || !Original->WidgetTree || !Original->ParentClass
            || Original->GetOutermost()->IsDirty())
        {
            UE_LOG(LogTemp, Error, TEXT("XmlUI Re-bake Apply: original WBP is invalid or dirty"));
            return false;
        }

        FMetaData& Metadata = Original->GetOutermost()->GetMetaData();
        const FString Baseline = Metadata.GetValue(Original, TEXT("XmlUI.SourceDsl"));
        const FString BaselineHash = Metadata.GetValue(Original, TEXT("XmlUI.SourceHash"));
        if (Baseline.IsEmpty() || BaselineHash.IsEmpty()
            || FMD5::HashAnsiString(*Baseline) != BaselineHash)
        {
            UE_LOG(LogTemp, Error, TEXT("XmlUI Re-bake Apply: no valid source baseline; unmanaged WBP cannot be updated"));
            return false;
        }

        FString Error;
        FString InputXml;
        if (!FFileHelper::LoadFileToString(InputXml, *XmlPath))
        {
            UE_LOG(LogTemp, Error, TEXT("XmlUI Re-bake Apply: could not read %s"), *XmlPath);
            return false;
        }

        const FString Token = FGuid::NewGuid().ToString(EGuidFormats::Digits);
        const FString AssetPath = TEXT("/Game/__XmlUIRebakePreview/WBP_XmlUIApplyStage_") + Token;
        const FString BeforeFile = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("XmlUIApply_") + Token + TEXT("_before.xml"));
        const FString StagedFile = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("XmlUIApply_") + Token + TEXT("_staged.xml"));
        const FString AppliedFile = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("XmlUIApply_") + Token + TEXT("_applied.xml"));
        FScopedPreviewArtifacts Artifacts;
        Artifacts.XmlFiles = { BeforeFile, StagedFile, AppliedFile };

        if (!FXmlUIDslExporter::ExportWbpToDslFile(WbpPath, BeforeFile))
        {
            UE_LOG(LogTemp, Error, TEXT("XmlUI Re-bake Apply: source export failed"));
            return false;
        }
        FXmlNodeDesc Before, Staged, Applied;
        if (!ParseFile(BeforeFile, Before, Error)
            || !BakeAndReadCanonical(XmlPath, AssetPath, StagedFile, Artifacts, Staged, Error))
        {
            UE_LOG(LogTemp, Error, TEXT("XmlUI Re-bake Apply: staging failed: %s"), *Error);
            return false;
        }

        FXmlRebakeDiff Change;
        DiffNodes(Before, Staged, FString(), Change);
        if (Change.bStructureChanged)
        {
            UE_LOG(LogTemp, Error, TEXT("XmlUI Re-bake Apply: structural modifications are not supported"));
            PrintDiff(TEXT("REJECTED"), Change);
            return false;
        }
        UWidgetBlueprint* StagedBP = LoadObject<UWidgetBlueprint>(nullptr, *AssetPath);
        if (!StagedBP || !StagedBP->WidgetTree
            || !CopyChangedProperties(Before, Staged, Original->WidgetTree,
                StagedBP->WidgetTree, true, Error))
        {
            UE_LOG(LogTemp, Error, TEXT("XmlUI Re-bake Apply: preflight failed: %s"), *Error);
            return false;
        }

        const FString OriginalPackageFile = FPackageName::LongPackageNameToFilename(
            Original->GetOutermost()->GetName(), TEXT(".uasset"));
        const FString BackupDirectory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("XmlUIBackups"));
        const FString BackupFile = FPaths::Combine(BackupDirectory,
            Original->GetName() + TEXT("_") + Token + TEXT(".uasset"));
        IFileManager::Get().MakeDirectory(*BackupDirectory, true);
        if (!IFileManager::Get().FileExists(*OriginalPackageFile)
            || !FPlatformFileManager::Get().GetPlatformFile().CopyFile(*BackupFile, *OriginalPackageFile))
        {
            UE_LOG(LogTemp, Error, TEXT("XmlUI Re-bake Apply: backup failed; source asset untouched"));
            return false;
        }

        TStrongObjectPtr<UWidgetTree> Snapshot(
            DuplicateObject<UWidgetTree>(Original->WidgetTree, GetTransientPackage()));
        if (!Snapshot)
        {
            UE_LOG(LogTemp, Error, TEXT("XmlUI Re-bake Apply: could not snapshot WidgetTree"));
            return false;
        }

        FScopedTransaction Transaction(NSLOCTEXT("XmlUI", "SafeRebakeApply", "XmlUI Safe Re-bake Apply"));
        Original->Modify();
        Original->WidgetTree->Modify();
        TArray<UWidget*> ExistingWidgets;
        Original->WidgetTree->GetAllWidgets(ExistingWidgets);
        for (UWidget* Widget : ExistingWidgets) Widget->Modify();

        const auto Rollback = [&](const FString& Reason, bool bRestoreDisk) -> bool
        {
            Transaction.Cancel();
            FString RestoreError;
            if (!CopyChangedProperties(Before, Staged, Original->WidgetTree,
                Snapshot.Get(), false, RestoreError))
            {
                UE_LOG(LogTemp, Error, TEXT("XmlUI Re-bake Apply: in-memory rollback failed: %s"),
                    *RestoreError);
            }
            FKismetEditorUtilities::CompileBlueprint(Original);
            Metadata.SetValue(Original, TEXT("XmlUI.SourceDsl"), *Baseline);
            Metadata.SetValue(Original, TEXT("XmlUI.SourceHash"), *BaselineHash);
            if (bRestoreDisk && !FPlatformFileManager::Get().GetPlatformFile()
                .CopyFile(*OriginalPackageFile, *BackupFile))
            {
                UE_LOG(LogTemp, Error, TEXT("XmlUI Re-bake Apply: disk rollback FAILED; restore manually from %s"),
                    *BackupFile);
            }
            FText ReloadError;
            if (!UPackageTools::ReloadPackages({ Original->GetOutermost() }, ReloadError, false))
            {
                UE_LOG(LogTemp, Error,
                    TEXT("XmlUI Re-bake Apply: package reload after rollback failed: %s; reopen WBP from %s"),
                    *ReloadError.ToString(), *BackupFile);
            }
            UE_LOG(LogTemp, Error, TEXT("XmlUI Re-bake Apply: FAILED: %s. Backup: %s"),
                *Reason, *BackupFile);
            return false;
        };

        if (!CopyChangedProperties(Before, Staged, Original->WidgetTree,
            StagedBP->WidgetTree, false, Error))
        {
            return Rollback(Error, false);
        }

        FBlueprintEditorUtils::MarkBlueprintAsModified(Original);
        FKismetEditorUtilities::CompileBlueprint(Original);
        if (Original->Status == BS_Error || !VerifyTextTemplates(Original, Error)
            || !VerifyCompiledChanges(Before, Staged, Original, StagedBP, Error))
        {
            return Rollback(Error.IsEmpty() ? TEXT("Blueprint compilation failed") : Error, false);
        }

        if (!FXmlUIDslExporter::ExportWbpToDslFile(WbpPath, AppliedFile)
            || !ParseFile(AppliedFile, Applied, Error))
        {
            return Rollback(TEXT("could not verify updated WBP export: ") + Error, false);
        }
        FXmlRebakeDiff Verify;
        DiffNodes(Staged, Applied, FString(), Verify);
        if (Verify.Changed)
        {
            PrintDiff(TEXT("VERIFY-FAILED"), Verify);
            return Rollback(TEXT("in-place result differs from staged candidate"), false);
        }

        Metadata.SetValue(Original, TEXT("XmlUI.SourceDsl"), *InputXml);
        Metadata.SetValue(Original, TEXT("XmlUI.SourceHash"), *FMD5::HashAnsiString(*InputXml));
        Metadata.SetValue(Original, TEXT("XmlUI.BakeVersion"), TEXT("1"));
        Original->MarkPackageDirty();
        const TArray<UPackage*> ChangedPackages{ Original->GetOutermost() };
        if (!UEditorLoadingAndSavingUtils::SavePackages(ChangedPackages, false))
        {
            // SavePackages may already have touched the original file.
            return Rollback(TEXT("saving updated WBP failed"), true);
        }

        UE_LOG(LogTemp, Display,
            TEXT("XmlUI Re-bake Apply: SUCCESS — %d properties; original WBP kept, backup=%s"),
            Change.Changed, *BackupFile);
        return true;
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

static FAutoConsoleCommand GXmlUIRebakeApplyCommand(
    TEXT("XmlUI.RebakeApply"),
    TEXT("In-place property-only Re-bake. Usage: XmlUI.RebakeApply Wbp=/Game/UI/WBP_Name File=/absolute/path/ui.xml Confirm=YES"),
    FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
    {
        const FString CommandLine = FString::Join(Args, TEXT(" "));
        FString WbpPath, XmlPath, Confirmation;
        FParse::Value(*CommandLine, TEXT("Wbp="), WbpPath);
        FParse::Value(*CommandLine, TEXT("File="), XmlPath);
        FParse::Value(*CommandLine, TEXT("Confirm="), Confirmation);
        Apply(WbpPath, XmlPath, Confirmation);
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

    TestTrue(TEXT("text can be applied"), IsAllowedChange(TEXT("Text"), TEXT("Text")));
    TestTrue(TEXT("font can be applied"), IsAllowedChange(TEXT("Text"), TEXT("Visual.Font")));
    TestTrue(TEXT("button style can be applied"), IsAllowedChange(TEXT("Button"), TEXT("Visual.WidgetStyle")));
    TestFalse(TEXT("layout slot change rejected"), IsAllowedChange(TEXT("Text"), TEXT("HAlign")));
    TestFalse(TEXT("arbitrary default override rejected"), IsAllowedChange(TEXT("Horizontal"), TEXT("Default.SomeProperty")));
    return true;
}
#endif
