using UnrealBuildTool;

public class XmlUI : ModuleRules
{
	public XmlUI(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"UMG",
			"Slate",
			"SlateCore",
			"InputCore",
			"XmlParser",
			"DeveloperSettings",
		});
	}
}
