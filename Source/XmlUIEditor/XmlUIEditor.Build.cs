using UnrealBuildTool;

public class XmlUIEditor : ModuleRules
{
	public XmlUIEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"XmlUI",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"CoreUObject",
			"Engine",
			"UMG",
			"Slate",
			"SlateCore",
			"InputCore",
			"UnrealEd",
			"ToolMenus",
			"UMGEditor",
			"Kismet",
			"AssetTools",
			"DesktopPlatform",
			"LevelEditor",
			"ApplicationCore",
		});
	}
}
