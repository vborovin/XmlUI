#include "XmlUIBaker.h"

#include "ToolMenus.h"
#include "DesktopPlatformModule.h"
#include "XmlBuilder.h"
#include "XmlDslParser.h"
#include "XmlUISettings.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "WidgetBlueprint.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/WidgetTree.h"
#include "FileHelpers.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/MessageDialog.h"
#include "Misc/Paths.h"

#define LOCTEXT_NAMESPACE "XmlUIBaker"

namespace
{
    bool ValidateNodeNamesRecursive(const FXmlNodeDesc& Node, TMap<FString, int32>& NameCount, FString& OutError)
    {
        if (Node.Name.IsEmpty())
        {
            OutError = FString::Printf(TEXT("XmlUI: DSL node (tag %s) is missing the Name attribute; a nameless widget in the UMG asset tree would be handled incorrectly by the compiler"), *Node.Tag);
            return false;
        }

        for (const TCHAR Ch : Node.Name)
        {
            const bool bIsAlnum = (Ch >= TEXT('A') && Ch <= TEXT('Z')) || (Ch >= TEXT('a') && Ch <= TEXT('z')) || (Ch >= TEXT('0') && Ch <= TEXT('9'));
            if (!bIsAlnum && Ch != TEXT('_'))
            {
                OutError = FString::Printf(TEXT("XmlUI: DSL node Name \"%s\" contains illegal character '%c'; only letters, digits, and underscores are allowed"), *Node.Name, Ch);
                return false;
            }
        }

        int32& Count = NameCount.FindOrAdd(Node.Name);
        ++Count;
        if (Count > 1)
        {
            OutError = FString::Printf(TEXT("XmlUI: DSL node Name \"%s\" is duplicated; widget names must be unique in the UMG asset tree"), *Node.Name);
            return false;
        }

        for (const FXmlNodeDesc& Child : Node.Children)
        {
            if (!ValidateNodeNamesRecursive(Child, NameCount, OutError))
            {
                return false;
            }
        }

        return true;
    }

    FString SanitizeAssetName(const FString& InName)
    {
        FString Result = InName;
        for (TCHAR& Ch : Result)
        {
            if (Ch == TEXT(' ') || Ch == TEXT('-') || Ch == TEXT('.'))
            {
                Ch = TEXT('_');
            }
        }
        return Result;
    }
}

void FXmlUIBaker::RegisterMenus()
{
    UToolMenus* ToolMenus = UToolMenus::Get();
    if (!ToolMenus)
    {
        return;
    }

    UToolMenu* Menu = ToolMenus->ExtendMenu("LevelEditor.MainMenu");
    if (!Menu)
    {
        return;
    }

    FToolMenuSection& Section = Menu->AddSection("XmlUI", LOCTEXT("XmlUI", "XmlUI"));
    Section.AddSubMenu(
        "XmlUI_SubMenu",
        LOCTEXT("XmlUI", "XmlUI"),
        LOCTEXT("XmlUI_Tooltip", "XmlUI tools"),
        FNewToolMenuDelegate::CreateLambda([](UToolMenu* SubMenu)
        {
            FToolMenuSection& SubSection = SubMenu->AddSection("XmlUI", LOCTEXT("XmlUI", "XmlUI"));
            SubSection.AddMenuEntry(
                "BakeXmlUI",
                LOCTEXT("BakeXmlUI", "XmlUI: Bake DSL to Widget Blueprint"),
                LOCTEXT("BakeXmlUITooltip", "Select a DSL (xml) file and statically generate a Widget Blueprint asset"),
                FSlateIcon(),
                FToolUIActionChoice(FExecuteAction::CreateLambda([]() { FXmlUIBaker::RunBakeFromDialog(); })));
        }));
}

void FXmlUIBaker::RunBakeFromDialog()
{
    IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
    if (!DesktopPlatform)
    {
        FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("NoDesktopPlatform", "XmlUI: Failed to get the desktop platform module (IDesktopPlatform)"));
        return;
    }

    TArray<FString> OutFiles;
    const FString InitialDir = GetDefault<UXmlUISettings>()->XmlRootPath.IsEmpty()
        ? FPaths::ProjectDir()
        : GetDefault<UXmlUISettings>()->XmlRootPath;
    if (!DesktopPlatform->OpenFileDialog(nullptr, TEXT("XmlUI: Select a DSL file"), InitialDir, TEXT(""),
        TEXT("XmlUI DSL|*.xml|All files|*.*"), EFileDialogFlags::None, OutFiles))
    {
        return;
    }

    if (OutFiles.Num() == 0)
    {
        return;
    }

    const FString DslFile = OutFiles[0];
    const FString BakedRootPath = GetDefault<UXmlUISettings>()->BakedBlueprintOutputPath;
    const FString OutAssetPath = FString::Printf(TEXT("%s/WBP_%s"), *BakedRootPath, *SanitizeAssetName(FPaths::GetBaseFilename(DslFile)));

    FString OutError;
    UWidgetBlueprint* BP = BakeDslToWidgetBlueprint(DslFile, OutAssetPath, OutError);
    if (BP)
    {
        UE_LOG(LogTemp, Log, TEXT("XmlUI: Bake succeeded: %s"), *OutAssetPath);
        FMessageDialog::Open(EAppMsgType::Ok, FText::Format(LOCTEXT("BakeSucceed", "XmlUI: Bake succeeded: {0}"), FText::FromString(OutAssetPath)));
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("XmlUI: Bake failed: %s"), *OutError);
        FMessageDialog::Open(EAppMsgType::Ok, FText::Format(LOCTEXT("BakeFailed", "XmlUI: Bake failed: {0}"), FText::FromString(OutError)));
    }
}

UWidgetBlueprint* FXmlUIBaker::BakeDslToWidgetBlueprint(const FString& DslFilePath, const FString& OutAssetPath, FString& OutError)
{
    FString XmlContent;
    if (!FFileHelper::LoadFileToString(XmlContent, *DslFilePath))
    {
        OutError = FString::Printf(TEXT("XmlUI: Failed to read the DSL file: %s"), *DslFilePath);
        return nullptr;
    }

    FXmlNodeDesc RootDesc;
    if (!UXmlDslParser::ParseXmlString(XmlContent, RootDesc, OutError))
    {
        return nullptr;
    }

    {
        TMap<FString, int32> NameCount;
        if (!ValidateNodeNamesRecursive(RootDesc, NameCount, OutError))
        {
            return nullptr;
        }
    }

    if (StaticLoadObject(UObject::StaticClass(), nullptr, *OutAssetPath, nullptr, LOAD_NoWarn))
    {
        OutError = FString::Printf(TEXT("XmlUI: Asset already exists: %s (delete the old asset first or change the output path)"), *OutAssetPath);
        return nullptr;
    }

    const FString AssetName = FPaths::GetBaseFilename(OutAssetPath);
    UPackage* Pkg = CreatePackage(*OutAssetPath);
    if (!Pkg)
    {
        OutError = FString::Printf(TEXT("XmlUI: Failed to create package: %s"), *OutAssetPath);
        return nullptr;
    }

    UClass* ParentClass = GetDefault<UXmlUISettings>()->BaseWidgetClass.LoadSynchronous();
    if (!ParentClass)
    {
        ParentClass = UUserWidget::StaticClass();
    }
    const FString* ParentClassPath = RootDesc.Attributes.Find(TEXT("ParentClass"));
    if (ParentClassPath && !ParentClassPath->IsEmpty())
    {
        UClass* CustomParent = LoadClass<UUserWidget>(nullptr, **ParentClassPath);
        if (!CustomParent)
        {
            OutError = FString::Printf(TEXT("XmlUI: Failed to load ParentClass '%s'; check the path (format /Script/<Module>.<ClassName>)"), **ParentClassPath);
            return nullptr;
        }
        ParentClass = CustomParent;
    }

    UWidgetBlueprint* BP = CastChecked<UWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
        ParentClass, Pkg, FName(*AssetName), BPTYPE_Normal,
        UWidgetBlueprint::StaticClass(), UWidgetBlueprintGeneratedClass::StaticClass()));

    UWidget* Root = UXmlBuilder::BuildNode(BP->WidgetTree, RootDesc, OutError);
    if (!Root)
    {
        BP->MarkAsGarbage();
        return nullptr;
    }
    BP->WidgetTree->RootWidget = Root;

    FKismetEditorUtilities::CompileBlueprint(BP);

    BP->MarkPackageDirty();
    const TArray<UPackage*> Packages{ Pkg };
    if (!UEditorLoadingAndSavingUtils::SavePackages(Packages, false))
    {
        OutError = FString::Printf(TEXT("XmlUI: Failed to save asset: %s"), *OutAssetPath);
        return nullptr;
    }

    return BP;
}

#undef LOCTEXT_NAMESPACE
