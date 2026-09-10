#include "UI/YUFSResultsWidget.h"

#include "Simulation/YUFSGameInstance.h"
#include "UI/YUFSResultEntryObject.h"

void UYUFSResultsWidget::NativeConstruct()
{
	Super::NativeConstruct();
	RefreshResults();
}

UYUFSGameInstance* UYUFSResultsWidget::GetYUFSGameInstance() const
{
	return GetGameInstance() ? Cast<UYUFSGameInstance>(GetGameInstance()) : nullptr;
}

void UYUFSResultsWidget::RefreshResults()
{
	Results.Empty();
	if (const UYUFSGameInstance* GI = GetYUFSGameInstance())
	{
		Results = GI->LastRunResults;
	}
	OnResultsRefreshed();
}

FSimRunResult UYUFSResultsWidget::GetRunAt(int32 Index) const
{
	return Results.IsValidIndex(Index) ? Results[Index] : FSimRunResult();
}

FText UYUFSResultsWidget::GetRunSummaryText(int32 Index) const
{
	if (!Results.IsValidIndex(Index))
	{
		return FText::GetEmpty();
	}

	const FSimRunResult& R = Results[Index];
	const FString ScenarioName = R.ScenarioConfig.DisplayName.IsEmpty()
		? TEXT("시나리오") : R.ScenarioConfig.DisplayName.ToString();

	return FText::FromString(FString::Printf(
		TEXT("[%s] %s | 대피 %d/%d (%.1f%%) | 사망 %d | 소요 %.1f초"),
		*R.Timestamp.ToString(TEXT("%Y-%m-%d %H:%M")), *ScenarioName,
		R.EvacuatedCount, R.TotalNPCCount, R.EvacuationRate * 100.f,
		R.IncapacitatedCount, R.SimDurationSeconds));
}

float UYUFSResultsWidget::GetAverageEvacuationRate() const
{
	if (Results.Num() == 0) return 0.f;

	float Sum = 0.f;
	for (const FSimRunResult& R : Results) Sum += R.EvacuationRate;
	return Sum / Results.Num();
}

float UYUFSResultsWidget::GetAverageDurationSeconds() const
{
	if (Results.Num() == 0) return 0.f;

	float Sum = 0.f;
	for (const FSimRunResult& R : Results) Sum += R.SimDurationSeconds;
	return Sum / Results.Num();
}

float UYUFSResultsWidget::GetAverageEvacuationTime() const
{
	float Sum = 0.f;
	int32 Count = 0;
	for (const FSimRunResult& R : Results)
	{
		if (R.EvacuatedCount > 0)
		{
			Sum += R.AverageEvacuationTime;
			++Count;
		}
	}
	return Count > 0 ? Sum / Count : 0.f;
}

int32 UYUFSResultsWidget::GetTotalEvacuatedCount() const
{
	int32 Sum = 0;
	for (const FSimRunResult& R : Results) Sum += R.EvacuatedCount;
	return Sum;
}

int32 UYUFSResultsWidget::GetTotalIncapacitatedCount() const
{
	int32 Sum = 0;
	for (const FSimRunResult& R : Results) Sum += R.IncapacitatedCount;
	return Sum;
}

int32 UYUFSResultsWidget::GetTotalNPCCount() const
{
	int32 Sum = 0;
	for (const FSimRunResult& R : Results) Sum += R.TotalNPCCount;
	return Sum;
}

FSimRunResult UYUFSResultsWidget::GetBestRun() const
{
	if (Results.Num() == 0) return FSimRunResult();

	const FSimRunResult* Best = &Results[0];
	for (const FSimRunResult& R : Results)
	{
		if (R.EvacuationRate > Best->EvacuationRate) Best = &R;
	}
	return *Best;
}

FSimRunResult UYUFSResultsWidget::GetWorstRun() const
{
	if (Results.Num() == 0) return FSimRunResult();

	const FSimRunResult* Worst = &Results[0];
	for (const FSimRunResult& R : Results)
	{
		if (R.EvacuationRate < Worst->EvacuationRate) Worst = &R;
	}
	return *Worst;
}

FText UYUFSResultsWidget::GetSummaryText() const
{
	if (Results.Num() == 0)
	{
		return FText::FromString(TEXT("표시할 결과가 없습니다."));
	}

	return FText::FromString(FString::Printf(
		TEXT("총 %d회차 | 평균 대피율 %.1f%% | 평균 소요시간 %.1f초"),
		Results.Num(), GetAverageEvacuationRate() * 100.f, GetAverageDurationSeconds()));
}

TArray<UObject*> UYUFSResultsWidget::GetResultEntries()
{
	TArray<UObject*> Entries;
	Entries.Reserve(Results.Num());

	for (const FSimRunResult& R : Results)
	{
		UYUFSResultEntryObject* Entry = NewObject<UYUFSResultEntryObject>(this);
		Entry->RunResult = R;
		Entries.Add(Entry);
	}

	return Entries;
}
