#include "Framework/Docking/TabManager.h"
#include "Modules/ModuleManager.h"
#include "SpaceMMOAuthoringLog.h"
#include "SpaceMMOAuthoringPanel.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

#define LOCTEXT_NAMESPACE "SpaceMMOAuthoring"

DEFINE_LOG_CATEGORY(LogSpaceMMOAuthoring);

namespace
{
	const FName WorldAuthoringTabName(TEXT("SpaceMMOWorldAuthoring"));
}

/**
 * Editor-only tools for authoring what lives in <c>data/</c>.
 *
 * One menu entry and one tab. Everything the tool does is in the panel; this exists to put it
 * somewhere findable, because a tool nobody can open is indistinguishable from one that was never
 * built.
 */
class FSpaceMMOAuthoringModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		FGlobalTabmanager::Get()
			->RegisterNomadTabSpawner(
				WorldAuthoringTabName,
				FOnSpawnTab::CreateStatic(&FSpaceMMOAuthoringModule::SpawnPanel))
			.SetDisplayName(LOCTEXT("TabTitle", "SpaceMMO World Authoring"))
			.SetTooltipText(LOCTEXT(
				"TabTooltip",
				"Place deposits and stations on a body by dragging them, and write "
				"data/universe/origin.json back out."))
			.SetGroup(WorkspaceMenu::GetMenuStructure().GetLevelEditorCategory());

		// Deferred, because ToolMenus may not have registered its own menus yet at startup and
		// adding to a menu that does not exist yet silently does nothing.
		//
		// Bound to this rather than to a static, so the unregister below actually matches it: an
		// unregister that quietly fails leaves a dangling callback across a hot reload.
		UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(
			this, &FSpaceMMOAuthoringModule::RegisterMenus));
	}

	virtual void ShutdownModule() override
	{
		UToolMenus::UnRegisterStartupCallback(this);

		// By the same name the entries were added under, which is what owns them.
		UToolMenus::UnregisterOwner(FToolMenuOwner(TEXT("SpaceMMOAuthoring")));

		FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(WorldAuthoringTabName);
	}

private:
	static TSharedRef<SDockTab> SpawnPanel(const FSpawnTabArgs& Args)
	{
		return SNew(SDockTab)
			.TabRole(ETabRole::NomadTab)
			[
				SNew(SSpaceMMOAuthoringPanel)
			];
	}

	void RegisterMenus()
	{
		FToolMenuOwnerScoped OwnerScoped(TEXT("SpaceMMOAuthoring"));

		UToolMenu* const Menu = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.MainMenu.Tools"));

		if (Menu == nullptr)
		{
			UE_LOG(LogSpaceMMOAuthoring, Warning,
				TEXT("No Tools menu to add to; open the panel from Window instead."));

			return;
		}

		FToolMenuSection& Section =
			Menu->FindOrAddSection(TEXT("SpaceMMO"), LOCTEXT("MenuSection", "SpaceMMO"));

		Section.AddMenuEntry(
			TEXT("SpaceMMOWorldAuthoring"),
			LOCTEXT("MenuEntry", "World Authoring"),
			LOCTEXT(
				"MenuEntryTooltip",
				"Draw a body, drag what is authored on it, and write origin.json back out."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda(
				[]()
				{
					FGlobalTabmanager::Get()->TryInvokeTab(WorldAuthoringTabName);
				})));
	}
};

IMPLEMENT_MODULE(FSpaceMMOAuthoringModule, SpaceMMOAuthoring);

#undef LOCTEXT_NAMESPACE
