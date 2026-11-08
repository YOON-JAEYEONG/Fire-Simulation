#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Components/ComboBoxString.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "UI/YUFSScenarioSetupWidget.h"
#include "UObject/UnrealType.h"

namespace YUFSScenarioMapPreviewTests
{
	static FYUFSScenarioMapOption MakeMap(const TCHAR* Path, UTexture2D* Thumbnail)
	{
		FYUFSScenarioMapOption Option;
		Option.DisplayName = FText::FromString(TEXT("Same map name"));
		Option.Map = TSoftObjectPtr<UWorld>(FSoftObjectPath(Path));
		Option.Thumbnail = Thumbnail;
		return Option;
	}

	// Assign optional widget bindings without loading or changing the user's WBP asset.
	static void BindWidget(UYUFSScenarioSetupWidget* Owner, const FName Name, UObject* Value)
	{
		FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(Owner->GetClass(), Name);
		check(Property);
		Property->SetObjectPropertyValue_InContainer(Owner, Value);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSScenarioMapPreviewSelectionTest,
	"YUFS.UI.ScenarioMapPreview.SelectionAndMissingThumbnail",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FYUFSScenarioMapPreviewSelectionTest::RunTest(const FString& Parameters)
{
	using namespace YUFSScenarioMapPreviewTests;
	auto* Widget = NewObject<UYUFSScenarioSetupWidget>();
	Widget->Initialize();
	auto* Combo = NewObject<UComboBoxString>(Widget);
	auto* Image = NewObject<UImage>(Widget);
	auto* Hint = NewObject<UTextBlock>(Widget);
	BindWidget(Widget, TEXT("MapCombo"), Combo);
	BindWidget(Widget, TEXT("Image_MapThumbnail"), Image);
	BindWidget(Widget, TEXT("Text_MapPreviewHint"), Hint);
	UTexture2D* Texture = UTexture2D::CreateTransient(320, 160);
	if (!TestNotNull(TEXT("Transient thumbnail created"), Texture)) return false;

	Widget->AvailableMaps = {
		MakeMap(TEXT("/Game/Tests/PreviewA.PreviewA"), Texture),
		MakeMap(TEXT("/Game/Tests/PreviewB.PreviewB"), nullptr)
	};
	Widget->NativeConstruct();
	TestEqual(TEXT("Map options populated"), Combo->GetOptionCount(), 2);
	TestTrue(TEXT("Duplicate display names remain individually selectable"),
		Combo->GetOptionAtIndex(0) != Combo->GetOptionAtIndex(1));
	TestEqual(TEXT("First map selected on construction"), Widget->SelectedMapIndex, 0);
	TestTrue(TEXT("First thumbnail shown"), Image->GetBrush().GetResourceObject() == Texture);
	TestTrue(TEXT("Thumbnail is visible without intercepting input"), Image->GetVisibility() == ESlateVisibility::HitTestInvisible);
	TestTrue(TEXT("Texture aspect ratio retained"), Image->GetBrush().ImageSize == FVector2D(320, 160));
	TestTrue(TEXT("Hint hidden for a thumbnail"), Hint->GetVisibility() == ESlateVisibility::Hidden);

	Combo->SetSelectedIndex(1);
	TestEqual(TEXT("Combo selection updates the scenario"), Widget->SelectedMapIndex, 1);
	TestTrue(TEXT("Selected map saved to WorkingConfig"),
		Widget->WorkingConfig.SimulationMap.ToSoftObjectPath() == Widget->AvailableMaps[1].Map.ToSoftObjectPath());
	TestNull(TEXT("Missing thumbnail clears previous texture"), Image->GetBrush().GetResourceObject());
	TestTrue(TEXT("Missing thumbnail hides image"), Image->GetVisibility() == ESlateVisibility::Hidden);
	TestTrue(TEXT("Missing thumbnail shows hint"), Hint->GetVisibility() == ESlateVisibility::HitTestInvisible);
	TestFalse(TEXT("Missing thumbnail has a nonempty explanation"), Hint->GetText().IsEmpty());

	FYUFSScenarioConfig Config = Widget->WorkingConfig;
	Config.SimulationMap = Widget->AvailableMaps[0].Map;
	Widget->InitializeWith(Config);
	TestEqual(TEXT("InitializeWith restores combo selection"), Combo->GetSelectedIndex(), 0);
	TestTrue(TEXT("InitializeWith restores thumbnail"), Image->GetBrush().GetResourceObject() == Texture);

	Widget->AvailableMaps.Reset();
	Widget->RefreshMapOptions();
	TestEqual(TEXT("Removed options do not leave a selected index"), Widget->SelectedMapIndex, INDEX_NONE);
	TestEqual(TEXT("Empty map list empties combo"), Combo->GetOptionCount(), 0);
	TestTrue(TEXT("Programmatic option rebuild preserves supplied scenario map"),
		Widget->WorkingConfig.SimulationMap.ToSoftObjectPath() == Config.SimulationMap.ToSoftObjectPath());
	TestNull(TEXT("Empty map list clears texture"), Image->GetBrush().GetResourceObject());
	Widget->NativeDestruct();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSScenarioMapPreviewOptionalWidgetsTest,
	"YUFS.UI.ScenarioMapPreview.OptionalWidgetsAndReconstruction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FYUFSScenarioMapPreviewOptionalWidgetsTest::RunTest(const FString& Parameters)
{
	using namespace YUFSScenarioMapPreviewTests;
	auto* Widget = NewObject<UYUFSScenarioSetupWidget>();
	Widget->Initialize();
	Widget->AvailableMaps = {
		MakeMap(TEXT("/Game/Tests/PreviewA.PreviewA"), nullptr),
		MakeMap(TEXT("/Game/Tests/PreviewB.PreviewB"), nullptr)
	};
	Widget->NativeConstruct();
	Widget->SelectMapByIndex(1);
	TestEqual(TEXT("Map selection works without any optional widgets"), Widget->SelectedMapIndex, 1);
	Widget->SelectMapByIndex(99);
	TestEqual(TEXT("Invalid selection does not replace valid map"), Widget->SelectedMapIndex, 1);
	Widget->NativeDestruct();

	auto* Combo = NewObject<UComboBoxString>(Widget);
	BindWidget(Widget, TEXT("MapCombo"), Combo);
	Widget->NativeConstruct();
	TestEqual(TEXT("Reconstructed view retains map selection"), Combo->GetSelectedIndex(), 1);
	Widget->NativeDestruct();
	Combo->SetSelectedIndex(0);
	TestEqual(TEXT("Destruct removes native selection handler"), Widget->SelectedMapIndex, 1);
	Widget->NativeConstruct();
	TestEqual(TEXT("Reconstruction restores config instead of stale combo selection"), Combo->GetSelectedIndex(), 1);
	Combo->SetSelectedIndex(0);
	TestEqual(TEXT("Reconstruction rebinds native selection handler"), Widget->SelectedMapIndex, 0);
	Widget->NativeDestruct();
	return true;
}

#endif
