#include "XmlUIBaker.h"
#include "XmlUIDslExporter.h"
#include "XmlVisualStyle.h"

#include "Editor.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Components/TextBlock.h"
#include "Components/PanelWidget.h"
#include "Components/PanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/VerticalBox.h"
#include "Components/Overlay.h"
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

        // Match by stable widget name, not by position: moving B before A must
        // not masquerade as a rename or as an A->B property change.
        TMap<FString, int32> NewIndices;
        for (int32 I = 0; I < After.Children.Num(); ++I)
        {
            NewIndices.Add(After.Children[I].Name, I);
        }
        TSet<FString> OldNames;
        for (int32 I = 0; I < Before.Children.Num(); ++I)
        {
            const FXmlNodeDesc& Child = Before.Children[I];
            OldNames.Add(Child.Name);
            const int32* OtherIndex = NewIndices.Find(Child.Name);
            if (!OtherIndex)
            {
                Diff.Add(FString::Printf(TEXT("%s: removed child <%s Name=%s>"),
                    *Path, *Child.Tag, *Child.Name), true);
                continue;
            }
            if (I != *OtherIndex)
            {
                Diff.Add(FString::Printf(TEXT("%s/%s: index %d -> %d"),
                    *Path, *Child.Name, I, *OtherIndex), true);
            }
            DiffNodes(Child, After.Children[*OtherIndex], Path, Diff);
        }
        for (const FXmlNodeDesc& Child : After.Children)
        {
            if (!OldNames.Contains(Child.Name))
            {
                Diff.Add(FString::Printf(TEXT("%s: added child <%s Name=%s>"),
                    *Path, *Child.Tag, *Child.Name), true);
            }
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
                    continue;
                }
                // DeleteAsset can return after the editor marks the package as
                // deleted while the recently saved file still exists. Never
                // touch arbitrary assets: these are GUID-named, tool-owned paths.
                const bool bToolOwned = AssetPath.StartsWith(
                    TEXT("/Game/__XmlUIRebakePreview/WBP_XmlUIRebake_"))
                    || AssetPath.StartsWith(
                        TEXT("/Game/__XmlUIRebakePreview/WBP_XmlUIApplyStage_"));
                if (bToolOwned)
                {
                    const FString PhysicalFile = FPackageName::LongPackageNameToFilename(
                        AssetPath, TEXT(".uasset"));
                    if (IFileManager::Get().FileExists(*PhysicalFile)
                        && !IFileManager::Get().Delete(*PhysicalFile))
                    {
                        UE_LOG(LogTemp, Error,
                            TEXT("XmlUI Re-bake Preview: deleted asset still exists on disk: %s"),
                            *PhysicalFile);
                    }
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
            UE_LOG(LogTemp, Display,
                TEXT("XmlUI Re-bake Preview: UNMANAGED — %d requested changes; run XmlUI.AdoptBaseline on the saved source WBP after round-trip validation"),
                DesiredDiff.Changed);
            return false;
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


    struct FStructuralNode
    {
        const FXmlNodeDesc* Node = nullptr;
        FString ParentName;
    };

    void IndexStructuralNodes(const FXmlNodeDesc& Node, const FString& Parent,
        TMap<FString, FStructuralNode>& Index)
    {
        FStructuralNode Entry;
        Entry.Node = &Node;
        Entry.ParentName = Parent;
        Index.Add(Node.Name, Entry);
        for (const FXmlNodeDesc& Child : Node.Children)
        {
            IndexStructuralNodes(Child, Node.Name, Index);
        }
    }

    bool SameAttributes(const FXmlNodeDesc& A, const FXmlNodeDesc& B)
    {
        if (A.Attributes.Num() != B.Attributes.Num())
        {
            return false;
        }
        for (const TPair<FString, FString>& Pair : A.Attributes)
        {
            const FString* Value = B.Attributes.Find(Pair.Key);
            if (!Value || *Value != Pair.Value)
            {
                return false;
            }
        }
        return true;
    }

    bool IsSupportedStructuralParent(const UWidget* Widget)
    {
        return Widget && (Widget->IsA<UVerticalBox>()
            || Widget->IsA<UHorizontalBox>() || Widget->IsA<UOverlay>());
    }

    bool ValidateStructuralPlan(const FXmlNodeDesc& Before, const FXmlNodeDesc& After,
        const UWidgetTree* ExistingTree, const UWidgetTree* CandidateTree,
        int32& Added, int32& Reordered, FString& Error)
    {
        Added = 0;
        Reordered = 0;
        TMap<FString, FStructuralNode> Old, New;
        IndexStructuralNodes(Before, FString(), Old);
        IndexStructuralNodes(After, FString(), New);
        if (Old.Num() == 0
            || Before.Name != After.Name || Before.Tag != After.Tag)
        {
            Error = TEXT("root identity changed");
            return false;
        }

        for (const TPair<FString, FStructuralNode>& Pair : Old)
        {
            const FStructuralNode* Candidate = New.Find(Pair.Key);
            if (!Candidate)
            {
                Error = FString::Printf(TEXT("deleted widget '%s' (deletion is not supported)"), *Pair.Key);
                return false;
            }
            if (Candidate->ParentName != Pair.Value.ParentName)
            {
                Error = FString::Printf(TEXT("widget '%s' was reparented (not supported)"), *Pair.Key);
                return false;
            }
            if (Candidate->Node->Tag != Pair.Value.Node->Tag
                || !SameAttributes(*Candidate->Node, *Pair.Value.Node))
            {
                Error = FString::Printf(
                    TEXT("existing widget '%s' has a tag or property change; do property-only Apply separately"),
                    *Pair.Key);
                return false;
            }
            if (const UWidget* OldWidget = ExistingTree->FindWidget(FName(*Pair.Key)))
            {
                const UWidget* NewWidget = CandidateTree->FindWidget(FName(*Pair.Key));
                if (!NewWidget || OldWidget->GetClass() != NewWidget->GetClass())
                {
                    Error = FString::Printf(TEXT("widget '%s' changed its UClass"), *Pair.Key);
                    return false;
                }
            }
            else
            {
                Error = FString::Printf(TEXT("widget '%s' not found in existing tree"), *Pair.Key);
                return false;
            }
        }

        for (const TPair<FString, FStructuralNode>& Pair : New)
        {
            if (Old.Contains(Pair.Key))
            {
                continue;
            }
            const FXmlNodeDesc& Node = *Pair.Value.Node;
            if (!Node.Children.IsEmpty()
                || (Node.Tag != TEXT("Text") && Node.Tag != TEXT("Image")
                    && Node.Tag != TEXT("Spacer")))
            {
                Error = FString::Printf(TEXT("new '%s' must be a leaf Text, Image or Spacer"), *Pair.Key);
                return false;
            }
            const FStructuralNode* Parent = Old.Find(Pair.Value.ParentName);
            UWidget* ExistingParent = ExistingTree->FindWidget(FName(*Pair.Value.ParentName));
            const UWidget* StageParent = CandidateTree->FindWidget(FName(*Pair.Value.ParentName));
            const UWidget* StageChild = CandidateTree->FindWidget(FName(*Pair.Key));
            UPanelWidget* Panel = Cast<UPanelWidget>(ExistingParent);
            const UPanelWidget* SourcePanel = Cast<UPanelWidget>(StageParent);
            if (!Parent || !Panel || !SourcePanel
                || !IsSupportedStructuralParent(Panel)
                || Panel->GetClass() != SourcePanel->GetClass()
                || !StageChild || !StageChild->Slot
                || StageChild->Slot->GetClass() != Panel->GetSlotClass())
            {
                Error = FString::Printf(TEXT("new '%s' needs a supported existing panel and a compatible slot"),
                    *Pair.Key);
                return false;
            }
            ++Added;
        }

        // Reorder only inside existing supported panels; no cross-parent moves.
        for (const TPair<FString, FStructuralNode>& Pair : Old)
        {
            const FStructuralNode& Desired = New.FindChecked(Pair.Key);
            const FXmlNodeDesc& OldNode = *Pair.Value.Node;
            const FXmlNodeDesc& NewNode = *Desired.Node;
            bool bChangedOrder = OldNode.Children.Num() != NewNode.Children.Num();
            for (int32 I = 0; I < FMath::Min(OldNode.Children.Num(), NewNode.Children.Num()); ++I)
            {
                bChangedOrder |= OldNode.Children[I].Name != NewNode.Children[I].Name;
            }
            if (!bChangedOrder)
            {
                continue;
            }
            UPanelWidget* Panel = Cast<UPanelWidget>(
                ExistingTree->FindWidget(FName(*Pair.Key)));
            if (!Panel || !IsSupportedStructuralParent(Panel))
            {
                Error = FString::Printf(TEXT("cannot change children of unsupported panel '%s'"), *Pair.Key);
                return false;
            }
            if (Panel->GetChildrenCount() != OldNode.Children.Num())
            {
                Error = FString::Printf(TEXT("existing panel '%s' child count does not match source XML"),
                    *Pair.Key);
                return false;
            }
            ++Reordered;
        }
        return true;
    }

    bool ApplyStructuralPlan(const FXmlNodeDesc& Before, const FXmlNodeDesc& After,
        UWidgetTree* ExistingTree, UWidgetTree* CandidateTree, FString& Error)
    {
        TMap<FString, FStructuralNode> Old, New;
        IndexStructuralNodes(Before, FString(), Old);
        IndexStructuralNodes(After, FString(), New);

        // One pass per panel, including inserted children at their intended indices.
        for (const TPair<FString, FStructuralNode>& Pair : Old)
        {
            const FXmlNodeDesc& OldNode = *Pair.Value.Node;
            const FXmlNodeDesc& Desired = *New.FindChecked(Pair.Key).Node;
            bool bChanged = OldNode.Children.Num() != Desired.Children.Num();
            for (int32 I = 0; I < FMath::Min(OldNode.Children.Num(), Desired.Children.Num()); ++I)
            {
                bChanged |= OldNode.Children[I].Name != Desired.Children[I].Name;
            }
            if (!bChanged)
            {
                continue;
            }
            UPanelWidget* Panel = Cast<UPanelWidget>(
                ExistingTree->FindWidget(FName(*Pair.Key)));
            if (!Panel)
            {
                Error = FString::Printf(TEXT("panel '%s' disappeared"), *Pair.Key);
                return false;
            }
            Panel->Modify();
            for (int32 I = 0; I < Desired.Children.Num(); ++I)
            {
                const FString& Name = Desired.Children[I].Name;
                UWidget* Existing = ExistingTree->FindWidget(FName(*Name));
                if (!Existing)
                {
                    UWidget* StageWidget = CandidateTree->FindWidget(FName(*Name));
                    if (!StageWidget || !StageWidget->Slot)
                    {
                        Error = FString::Printf(TEXT("new staged widget '%s' has no slot"), *Name);
                        return false;
                    }
                    Existing = DuplicateObject<UWidget>(StageWidget, ExistingTree, FName(*Name));
                    if (!Existing || !Panel->InsertChildAt(I, Existing, StageWidget->Slot))
                    {
                        Error = FString::Printf(TEXT("cannot insert '%s' into '%s'"), *Name, *Pair.Key);
                        return false;
                    }
                }
                else if (Panel->GetChildAt(I) != Existing)
                {
                    if (!Panel->HasChild(Existing))
                    {
                        Error = FString::Printf(TEXT("existing '%s' is no longer child of '%s'"),
                            *Name, *Pair.Key);
                        return false;
                    }
                    Panel->ShiftChild(I, Existing);
                }
                if (Panel->GetChildAt(I) != Existing)
                {
                    Error = FString::Printf(TEXT("insertion/reorder mismatch for '%s'"), *Name);
                    return false;
                }
            }
            if (Panel->GetChildrenCount() != Desired.Children.Num())
            {
                Error = FString::Printf(TEXT("panel '%s' has an unexpected child count"), *Pair.Key);
                return false;
            }
        }
        return true;
    }

    bool CompareGeneratedStructure(const UWidget* Actual, const UWidget* Desired,
        FString& Error)
    {
        if (!Actual || !Desired || Actual->GetFName() != Desired->GetFName()
            || Actual->GetClass() != Desired->GetClass())
        {
            Error = TEXT("generated widget name or class mismatch");
            return false;
        }
        const UPanelWidget* ActualPanel = Cast<UPanelWidget>(Actual);
        const UPanelWidget* DesiredPanel = Cast<UPanelWidget>(Desired);
        if (static_cast<bool>(ActualPanel) != static_cast<bool>(DesiredPanel))
        {
            Error = FString::Printf(TEXT("%s: generated panel class mismatch"), *Actual->GetName());
            return false;
        }
        if (ActualPanel)
        {
            if (ActualPanel->GetChildrenCount() != DesiredPanel->GetChildrenCount())
            {
                Error = FString::Printf(TEXT("%s: generated child count mismatch"), *Actual->GetName());
                return false;
            }
            for (int32 I = 0; I < ActualPanel->GetChildrenCount(); ++I)
            {
                if (!CompareGeneratedStructure(ActualPanel->GetChildAt(I),
                    DesiredPanel->GetChildAt(I), Error))
                {
                    return false;
                }
            }
        }
        return true;
    }

    // Every source widget needs a stable Blueprint variable GUID (even when
    // bIsVariable is false). Adding a UWidget directly to a UWidgetTree does
    // not update WidgetVariableNameToGuidMap. UMG's compiler checks this
    // invariant in ValidateAndFixUpVariableGuids() before generating fields.
    // Also fixes widgets inserted by earlier XmlUI versions without GUIDs;
    // existing widget and animation GUIDs are never regenerated or removed.
    bool EnsureWidgetVariableGuids(const UWidgetTree* Tree,
        TMap<FName, FGuid>& GuidMap, int32& OutAssigned, FString& Error)
    {
        OutAssigned = 0;
        if (!Tree || !Tree->RootWidget)
        {
            Error = TEXT("cannot register widget GUIDs: WidgetTree is empty");
            return false;
        }

        TArray<UWidget*> Widgets;
        Tree->GetAllWidgets(Widgets);

        // Preflight all names before changing the map; existing GUIDs are
        // immutable because Blueprint graph references may depend on them.
        TSet<FName> Names;
        TSet<FGuid> UsedGuids;
        for (const TPair<FName, FGuid>& Pair : GuidMap)
        {
            if (!Pair.Value.IsValid() || UsedGuids.Contains(Pair.Value))
            {
                Error = FString::Printf(
                    TEXT("existing widget/animation GUID map has an invalid or duplicate GUID at '%s'"),
                    *Pair.Key.ToString());
                return false;
            }
            UsedGuids.Add(Pair.Value);
        }
        for (const UWidget* Widget : Widgets)
        {
            if (!Widget || Widget->GetFName().IsNone()
                || Names.Contains(Widget->GetFName()))
            {
                Error = TEXT("WidgetTree contains an unnamed or duplicate widget");
                return false;
            }
            Names.Add(Widget->GetFName());
        }

        for (const UWidget* Widget : Widgets)
        {
            const FName Name = Widget->GetFName();
            if (GuidMap.Contains(Name))
            {
                continue;
            }
            FGuid NewGuid;
            do
            {
                NewGuid = FGuid::NewGuid();
            }
            while (!NewGuid.IsValid() || UsedGuids.Contains(NewGuid));
            GuidMap.Add(Name, NewGuid);
            UsedGuids.Add(NewGuid);
            ++OutAssigned;
            UE_LOG(LogTemp, Display,
                TEXT("XmlUI Re-bake Apply: assigned GUID %s to widget '%s'"),
                *NewGuid.ToString(), *Name.ToString());
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
        UWidgetBlueprint* StagedBP = LoadObject<UWidgetBlueprint>(nullptr, *AssetPath);
        if (!StagedBP || !StagedBP->WidgetTree)
        {
            UE_LOG(LogTemp, Error, TEXT("XmlUI Re-bake Apply: staged WidgetTree not found"));
            return false;
        }
        int32 Added = 0, Reordered = 0;
        if (Change.bStructureChanged)
        {
            if (!ValidateStructuralPlan(Before, Staged, Original->WidgetTree,
                StagedBP->WidgetTree, Added, Reordered, Error))
            {
                UE_LOG(LogTemp, Error, TEXT("XmlUI Re-bake Apply: structural change rejected: %s"), *Error);
                return false;
            }
        }
        else if (!CopyChangedProperties(Before, Staged, Original->WidgetTree,
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
        TMap<FName, UWidget*> OriginalIdentities;
        for (UWidget* Widget : ExistingWidgets)
        {
            OriginalIdentities.Add(Widget->GetFName(), Widget);
        }
        // The GUID map is part of the WBP, not the WidgetTree. Keep its
        // complete state (including animation entries) for any rollback.
        const TMap<FName, FGuid> OriginalVariableGuids =
            Original->WidgetVariableNameToGuidMap;

        const auto Rollback = [&](const FString& Reason, bool bRestoreDisk) -> bool
        {
            Transaction.Cancel();
            Original->WidgetVariableNameToGuidMap = OriginalVariableGuids;
            FString RestoreError;
            if (!Change.bStructureChanged)
            {
                if (!CopyChangedProperties(Before, Staged, Original->WidgetTree,
                    Snapshot.Get(), false, RestoreError))
                {
                    UE_LOG(LogTemp, Error, TEXT("XmlUI Re-bake Apply: in-memory rollback failed: %s"),
                        *RestoreError);
                }
                FKismetEditorUtilities::CompileBlueprint(Original);
            }
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

        if (Change.bStructureChanged)
        {
            if (!ApplyStructuralPlan(Before, Staged, Original->WidgetTree,
                StagedBP->WidgetTree, Error))
            {
                return Rollback(Error, false);
            }
            for (const TPair<FName, UWidget*>& Pair : OriginalIdentities)
            {
                if (Original->WidgetTree->FindWidget(Pair.Key) != Pair.Value)
                {
                    return Rollback(FString::Printf(TEXT("existing widget '%s' lost UObject identity"),
                        *Pair.Key.ToString()), false);
                }
            }
            // This operation only changes the UMG widget tree, not the Blueprint's
            // declared functions, graphs or member variables. Structural marking
            // synchronously triggers a *second*, skeleton-only compilation and
            // editor notifications before the full compile below. With an open
            // Widget Blueprint editor this can hit a handled ensure during
            // skeleton reconstruction (even if the final WBP is correct).
            // Mark the Blueprint dirty here and perform exactly one explicit
            // full compilation after both edit paths.
            FBlueprintEditorUtils::MarkBlueprintAsModified(Original);
        }
        else
        {
            if (!CopyChangedProperties(Before, Staged, Original->WidgetTree,
                StagedBP->WidgetTree, false, Error))
            {
                return Rollback(Error, false);
            }
            FBlueprintEditorUtils::MarkBlueprintAsModified(Original);
        }
        // Register missing GUIDs *before* compilation. This also repairs a
        // widget left without a GUID by a previous structural Apply.
        int32 AssignedGuids = 0;
        if (!EnsureWidgetVariableGuids(Original->WidgetTree,
            Original->WidgetVariableNameToGuidMap, AssignedGuids, Error))
        {
            return Rollback(Error, false);
        }
        FKismetEditorUtilities::CompileBlueprint(Original);
        const UWidgetBlueprintGeneratedClass* RebuiltClass =
            Cast<UWidgetBlueprintGeneratedClass>(Original->GeneratedClass);
        const UWidgetBlueprintGeneratedClass* StagedClass =
            Cast<UWidgetBlueprintGeneratedClass>(StagedBP->GeneratedClass);
        const UWidgetTree* RebuiltTree =
            RebuiltClass ? RebuiltClass->GetWidgetTreeArchetype() : nullptr;
        const UWidgetTree* DesiredTree =
            StagedClass ? StagedClass->GetWidgetTreeArchetype() : nullptr;
        const bool bStructureValid = !Change.bStructureChanged
            || (RebuiltTree && DesiredTree && CompareGeneratedStructure(
                RebuiltTree->RootWidget, DesiredTree->RootWidget, Error));
        if (Original->Status == BS_Error || !VerifyTextTemplates(Original, Error)
            || !bStructureValid
            || (!Change.bStructureChanged
                && !VerifyCompiledChanges(Before, Staged, Original, StagedBP, Error)))
        {
            return Rollback(Error.IsEmpty() ? TEXT("Blueprint compilation failed") : Error, false);
        }
        // Compilation must not replace or renumber any existing Blueprint
        // widget/animation GUIDs, because graph references rely on them.
        for (const TPair<FName, FGuid>& Pair : OriginalVariableGuids)
        {
            const FGuid* Saved = Original->WidgetVariableNameToGuidMap.Find(Pair.Key);
            if (!Saved || *Saved != Pair.Value)
            {
                return Rollback(FString::Printf(
                    TEXT("original variable GUID changed for '%s'"),
                    *Pair.Key.ToString()), false);
            }
        }
        // Structural compilation must not invalidate any existing widget object.
        if (Change.bStructureChanged)
        {
            for (const TPair<FName, UWidget*>& Pair : OriginalIdentities)
            {
                if (Original->WidgetTree->FindWidget(Pair.Key) != Pair.Value)
                {
                    return Rollback(FString::Printf(TEXT("compile replaced existing widget '%s'"),
                        *Pair.Key.ToString()), false);
                }
            }
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
            TEXT("XmlUI Re-bake Apply: SUCCESS — %d changes (added=%d, modifiedPanels=%d, assignedGuids=%d); original WBP kept, backup=%s"),
            Change.Changed, Added, Reordered, AssignedGuids, *BackupFile);
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

    // Structural additions and reorders must not replace existing widget objects.
    const FString OldXml = TEXT("<Vertical Name=\"Root\"><Text Name=\"A\" Text=\"A\"/>"
        "<Text Name=\"B\" Text=\"B\"/></Vertical>");
    const FString NewXml = TEXT("<Vertical Name=\"Root\"><Text Name=\"B\" Text=\"B\"/>"
        "<Text Name=\"NewText\" Text=\"New\"/><Text Name=\"A\" Text=\"A\"/></Vertical>");
    FXmlNodeDesc OldStructure, NewStructure;
    TestTrue(TEXT("parse old structural XML"),
        UXmlDslParser::ParseXmlString(OldXml, OldStructure, Error));
    TestTrue(TEXT("parse staged structural XML"),
        UXmlDslParser::ParseXmlString(NewXml, NewStructure, Error));

    UWidgetTree* OldTree = NewObject<UWidgetTree>();
    UVerticalBox* OldRoot = OldTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Root"));
    OldTree->RootWidget = OldRoot;
    UTextBlock* OldA = OldTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("A"));
    UTextBlock* OldB = OldTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("B"));
    OldRoot->AddChild(OldA);
    OldRoot->AddChild(OldB);

    UWidgetTree* NewTree = NewObject<UWidgetTree>();
    UVerticalBox* NewRoot = NewTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Root"));
    NewTree->RootWidget = NewRoot;
    UTextBlock* NewB = NewTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("B"));
    UTextBlock* NewText = NewTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("NewText"));
    UTextBlock* NewA = NewTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("A"));
    NewRoot->AddChild(NewB);
    NewRoot->AddChild(NewText);
    NewRoot->AddChild(NewA);

    int32 AddedCount = 0, ReorderedPanels = 0;
    TestTrue(TEXT("valid leaf addition and reorder"),
        ValidateStructuralPlan(OldStructure, NewStructure, OldTree, NewTree,
            AddedCount, ReorderedPanels, Error));
    TestEqual(TEXT("one added widget"), AddedCount, 1);
    TestEqual(TEXT("one modified panel"), ReorderedPanels, 1);
    TestTrue(TEXT("apply structural patch to test tree"),
        ApplyStructuralPlan(OldStructure, NewStructure, OldTree, NewTree, Error));
    TestEqual(TEXT("three children after apply"), OldRoot->GetChildrenCount(), 3);
    TestTrue(TEXT("existing B preserved"), OldRoot->GetChildAt(0) == OldB);
    TestTrue(TEXT("new child inserted by name"), OldRoot->GetChildAt(1)
        && OldRoot->GetChildAt(1)->GetFName() == FName(TEXT("NewText")));
    TestTrue(TEXT("existing A preserved"), OldRoot->GetChildAt(2) == OldA);

    // Regression for UE5.8 WidgetBlueprintCompiler.cpp:781: an inserted
    // WidgetTree child must have its own GUID before Blueprint compilation.
    TMap<FName, FGuid> VariableGuids;
    const FGuid RootGuid = FGuid::NewGuid();
    const FGuid AGuid = FGuid::NewGuid();
    const FGuid BGuid = FGuid::NewGuid();
    VariableGuids.Add(TEXT("Root"), RootGuid);
    VariableGuids.Add(TEXT("A"), AGuid);
    VariableGuids.Add(TEXT("B"), BGuid);
    int32 Assigned = 0;
    TestTrue(TEXT("register inserted widget GUID"),
        EnsureWidgetVariableGuids(OldTree, VariableGuids, Assigned, Error));
    TestEqual(TEXT("one new GUID"), Assigned, 1);
    const FGuid* NewGuid = VariableGuids.Find(FName(TEXT("NewText")));
    TestTrue(TEXT("inserted widget GUID exists and is valid"),
        NewGuid && NewGuid->IsValid());
    TestTrue(TEXT("root GUID unchanged"), VariableGuids.FindChecked(FName(TEXT("Root"))) == RootGuid);
    TestTrue(TEXT("A GUID unchanged"), VariableGuids.FindChecked(FName(TEXT("A"))) == AGuid);
    TestTrue(TEXT("B GUID unchanged"), VariableGuids.FindChecked(FName(TEXT("B"))) == BGuid);
    const FGuid FirstNewGuid = NewGuid ? *NewGuid : FGuid();
    TestTrue(TEXT("re-registering same widget is safe"),
        EnsureWidgetVariableGuids(OldTree, VariableGuids, Assigned, Error));
    TestEqual(TEXT("no GUID regenerated"), Assigned, 0);
    TestTrue(TEXT("new widget GUID stays stable"),
        VariableGuids.FindChecked(FName(TEXT("NewText"))) == FirstNewGuid);

    // Earlier Structural Re-bake versions could leave an already-existing
    // widget without a GUID; the next Apply must repair it without replacing
    // any of the other GUIDs.
    VariableGuids.Remove(FName(TEXT("NewText")));
    TestTrue(TEXT("previously missing widget GUID can be repaired"),
        EnsureWidgetVariableGuids(OldTree, VariableGuids, Assigned, Error));
    TestEqual(TEXT("only one repaired GUID"), Assigned, 1);
    TestTrue(TEXT("repaired GUID is valid"),
        VariableGuids.FindChecked(FName(TEXT("NewText"))).IsValid());
    TestTrue(TEXT("original A GUID remains unchanged after repair"),
        VariableGuids.FindChecked(FName(TEXT("A"))) == AGuid);

    FXmlNodeDesc DeletedStructure;
    TestTrue(TEXT("parse deletion candidate"),
        UXmlDslParser::ParseXmlString(
            TEXT("<Vertical Name=\"Root\"><Text Name=\"A\" Text=\"A\"/></Vertical>"),
            DeletedStructure, Error));
    TestFalse(TEXT("deletion is rejected"),
        ValidateStructuralPlan(OldStructure, DeletedStructure, OldTree, NewTree,
            AddedCount, ReorderedPanels, Error));
    return true;
}
#endif
