#include "UI/YUFSResultRowWidget.h"

#include "Simulation/YUFSGameInstance.h"
#include "UI/YUFSResultEntryObject.h"

void UYUFSResultRowWidget::NativeOnListItemObjectSet(UObject* ListItemObject)
{
	if (const UYUFSResultEntryObject* Entry = Cast<UYUFSResultEntryObject>(ListItemObject))
	{
		CachedResult = Entry->RunResult;
	}
	OnRowDataSet();
}

FText UYUFSResultRowWidget::GetRunIndexText() const
{
	return FText::FromString(CachedResult.Timestamp.ToString(TEXT("%Y-%m-%d %H:%M")));
}

FText UYUFSResultRowWidget::GetEvacuationSummaryText() const
{
	return FText::FromString(FString::Printf(
		TEXT("%d / %d명 대피"), CachedResult.EvacuatedCount, CachedResult.TotalNPCCount));
}

FText UYUFSResultRowWidget::GetEvacuationRateText() const
{
	return FText::FromString(FString::Printf(TEXT("%.1f%%"), CachedResult.EvacuationRate * 100.f));
}

FText UYUFSResultRowWidget::GetIncapacitatedText() const
{
	return FText::FromString(FString::Printf(TEXT("사망 %d명"), CachedResult.IncapacitatedCount));
}

FText UYUFSResultRowWidget::GetDurationText() const
{
	return FText::FromString(FString::Printf(TEXT("%.1f초"), CachedResult.SimDurationSeconds));
}

FText UYUFSResultRowWidget::GetAverageEvacuationTimeText() const
{
	return FText::FromString(FString::Printf(TEXT("평균 대피 시간 %.1f초"), CachedResult.AverageEvacuationTime));
}

bool UYUFSResultRowWidget::ReplayThisRun()
{
	if (UYUFSGameInstance* GI = Cast<UYUFSGameInstance>(GetGameInstance()))
	{
		return GI->ReplayRun(CachedResult);
	}
	return false;
}

bool UYUFSResultRowWidget::CanReplayThisRun() const
{
	FText Error;
	return CachedResult.ScenarioConfig.IsValid(Error);
}
