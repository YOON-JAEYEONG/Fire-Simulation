// Fill out your copyright notice in the Description page of Project Settings.

#include "Simulation/YUFSGameInstance.h"

#include "Kismet/GameplayStatics.h"
#include "Simulation/YUFSRunResultsSaveGame.h"
#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "NPC/YUFSEvacuationNPC.h"
#include "NPC/Cognition/YUFSHumanCognitionComponent.h"
#include "NPC/Integration/YUFSBelongingsRetrievalComponent.h"
#include "Props/YUFSBelongingsBag.h"
#include "Simulation/YUFSSimulationController.h"
#include "Fire/YUFSBinaryManager.h"
#include "Level/YUFSLevelDataManager.h"
#include "Level/YUFSExitPoint.h"
#include "NPC/Integration/YUFSNpcEnvironmentInteraction.h"
#include "NPC/Navigation/YUFSSmokeAwareNavigator.h"
#include "NPC/Navigation/YUFSLocalMovementComponent.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "Misc/ConfigCacheIni.h"
#include "Debug/YUFSInteractionPreview.h"

void UYUFSGameInstance::OnStart()
{
	Super::OnStart();
	SetupBuildingInteractions(GetWorld());
}

namespace
{
	/**
	 * Test/demo helper: place NPCs on walkable floor inside the recorded fire's data domain
	 * (the building part covered by smoke/heat), without editing the level asset.
	 */
	int32 SpawnTestNPCs(UWorld* World, int32 Count)
	{
		auto* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		AYUFSBinaryManager* Binary = nullptr;
		for (TActorIterator<AYUFSBinaryManager> It(World); It; ++It) { Binary = *It; break; }
		FBox Domain(ForceInit);
		if (!Nav || !Binary || !Binary->GetGridWorldBounds(Domain))
		{
			UE_LOG(LogTemp, Warning, TEXT("[YUFSTestNPC] No navmesh or fire data domain; no test NPCs placed."));
			return 0;
		}
		UClass* NpcClass = LoadClass<AYUFSEvacuationNPC>(nullptr, TEXT("/Game/Blueprint/BP_YUFSRLEvacuationNPC.BP_YUFSRLEvacuationNPC_C"));
		if (!NpcClass) NpcClass = AYUFSEvacuationNPC::StaticClass();
		// NPC observations require a level data manager with exits. Maps still under construction
		// (e.g. Main) may lack them: add runtime-only test exits on walkable ground outside the domain.
		if (!TActorIterator<AYUFSLevelDataManager>(World))
		{
			int32 ExitCount = 0;
			for (TActorIterator<AYUFSExitPoint> It(World); It; ++It) ++ExitCount;
			if (ExitCount == 0)
			{
				FNavLocation Anchor;
				const FVector Center = Domain.GetCenter();
				if (Nav->ProjectPointToNavigation(FVector(Center.X, Center.Y, Domain.Min.Z + 60.f), Anchor, FVector(800.f, 800.f, 200.f)))
				{
					TArray<FVector> Exits;
					const float Radius = Domain.GetExtent().Size2D();
					for (const float Extra : { 300.f, 700.f, 1200.f })
					for (int32 Step = 0; Step < 24; ++Step)
					{
						const float Angle = 2.f * PI * Step / 24.f;
						const FVector Probe = Center + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * (Radius + Extra);
						FNavLocation Ground;
						if (!Nav->ProjectPointToNavigation(FVector(Probe.X, Probe.Y, Domain.Min.Z + 60.f), Ground, FVector(300.f, 300.f, 250.f))
							|| (Ground.Location.X > Domain.Min.X && Ground.Location.X < Domain.Max.X
								&& Ground.Location.Y > Domain.Min.Y && Ground.Location.Y < Domain.Max.Y)
							|| Exits.ContainsByPredicate([&Ground](const FVector& E) { return FVector::Dist2D(E, Ground.Location) < 1500.f; }))
							continue;
						const UNavigationPath* Path = UNavigationSystemV1::FindPathToLocationSynchronously(World, Anchor.Location, Ground.Location);
						if (Path && Path->IsValid() && !Path->IsPartial()) Exits.Add(Ground.Location);
						if (Exits.Num() >= 4) break;
					}
					// Navmesh covers only the interior: use reachable ground-floor points at the building edge.
					if (Exits.IsEmpty())
					{
						struct FCandidate { FVector Location; float EdgeDistance; };
						TArray<FCandidate> Candidates;
						const FVector Extent = Domain.GetExtent();
						for (int32 IX = 0; IX <= 30; ++IX)
						for (int32 IY = 0; IY <= 12; ++IY)
						{
							const FVector Probe(Domain.Min.X + 2.f * Extent.X * IX / 30.f, Domain.Min.Y + 2.f * Extent.Y * IY / 12.f, Domain.Min.Z + 60.f);
							FNavLocation Ground;
							if (!Nav->ProjectPointToNavigation(Probe, Ground, FVector(80.f, 80.f, 150.f))
								|| Ground.Location.Z > Domain.Min.Z + 150.f) continue;
							const float Edge = FMath::Min(
								FMath::Min(Ground.Location.X - Domain.Min.X, Domain.Max.X - Ground.Location.X),
								FMath::Min(Ground.Location.Y - Domain.Min.Y, Domain.Max.Y - Ground.Location.Y));
							Candidates.Add({ Ground.Location, Edge });
						}
						Candidates.Sort([](const FCandidate& A, const FCandidate& B) { return A.EdgeDistance < B.EdgeDistance; });
						for (const FCandidate& Candidate : Candidates)
						{
							if (Exits.Num() >= 4) break;
							if (Exits.ContainsByPredicate([&Candidate](const FVector& E) { return FVector::Dist2D(E, Candidate.Location) < 1500.f; })) continue;
							const UNavigationPath* Path = UNavigationSystemV1::FindPathToLocationSynchronously(World, Anchor.Location, Candidate.Location);
							if (Path && Path->IsValid() && !Path->IsPartial()) Exits.Add(Candidate.Location);
						}
					}
					for (int32 Index = 0; Index < Exits.Num(); ++Index)
						if (auto* Exit = World->SpawnActor<AYUFSExitPoint>(Exits[Index] + FVector(0, 0, 90.f), FRotator::ZeroRotator))
						{
							Exit->ExitID = FName(*FString::Printf(TEXT("TestExit_%d"), Index));
							Exit->bIsFamiliarEntry = Index == 0;
							UE_LOG(LogTemp, Display, TEXT("[YUFSTestNPC] test exit %d at %s"), Index, *Exits[Index].ToCompactString());
						}
					UE_LOG(LogTemp, Warning, TEXT("[YUFSTestNPC] Map has no exits: created %d runtime test exits outside the building (not saved to the level)."), Exits.Num());
				}
			}
			World->SpawnActor<AYUFSLevelDataManager>();
			UE_LOG(LogTemp, Warning, TEXT("[YUFSTestNPC] Map has no YUFSLevelDataManager: spawned one for this run (not saved to the level)."));
		}
		FRandomStream Rng(20260927);
		TArray<FVector> Placed;
		const FVector Size = Domain.GetSize();
		// Level-placed bags first: every bag gets an owner who starts empty-handed some 11-16 m
		// away, so the run shows "walk to the bag -> pick it up -> evacuate with it".
		for (TActorIterator<AYUFSBelongingsBag> It(World); It && Placed.Num() < Count; ++It)
		{
			if (!It->IsLevelPlaced() || It->IsClaimed()) continue;
			const FVector BagFloor = It->GetPickupLocation();
			for (int32 Try = 0; Try < 48; ++Try)
			{
				const float Distance = Rng.FRandRange(1100.f, 1600.f);
				const FVector Probe = BagFloor + FRotator(0.f, Try * 37.f, 0.f).Vector() * Distance + FVector(0, 0, 40.f);
				FNavLocation Floor;
				if (!Nav->ProjectPointToNavigation(Probe, Floor, FVector(80.f, 80.f, 120.f))
					|| FMath::Abs(Floor.Location.Z - BagFloor.Z) > 60.f
					|| FVector::Dist2D(Floor.Location, BagFloor) < 1000.f
					|| Placed.ContainsByPredicate([&Floor](const FVector& P) { return FVector::DistSquared(P, Floor.Location) < FMath::Square(150.f); }))
					continue;
				// The walk to the bag must exist and stay within the NPC's return budget (35 m).
				const UNavigationPath* Path = UNavigationSystemV1::FindPathToLocationSynchronously(World, Floor.Location, BagFloor);
				if (!Path || !Path->IsValid() || Path->IsPartial() || Path->GetPathLength() > 3000.f) continue;
				FActorSpawnParameters Spawn;
				Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButDontSpawnIfColliding;
				const FRotator Facing(0.f, Rng.FRandRange(-180.f, 180.f), 0.f);
				if (World->SpawnActor<AYUFSEvacuationNPC>(NpcClass, Floor.Location + FVector(0, 0, 95.f), Facing, Spawn))
				{
					Placed.Add(Floor.Location);
					break;
				}
			}
		}
		const int32 BagOwners = Placed.Num();
		for (int32 Attempt = 0; Attempt < Count * 40 && Placed.Num() < Count; ++Attempt)
		{
			const FVector Candidate(
				Domain.Min.X + Size.X * Rng.FRandRange(0.05f, 0.95f),
				Domain.Min.Y + Size.Y * Rng.FRandRange(0.05f, 0.95f),
				Domain.Min.Z + Size.Z * Rng.FRand());
			FNavLocation Floor;
			if (!Nav->ProjectPointToNavigation(Candidate, Floor, FVector(40.f, 40.f, Size.Z))
				|| !Domain.ExpandBy(FVector(0, 0, 50)).IsInside(Floor.Location)
				|| Placed.ContainsByPredicate([&Floor](const FVector& P) { return FVector::DistSquared(P, Floor.Location) < FMath::Square(150.f); }))
				continue;
			FActorSpawnParameters Spawn;
			Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButDontSpawnIfColliding;
			const FRotator Facing(0.f, Rng.FRandRange(-180.f, 180.f), 0.f);
			if (World->SpawnActor<AYUFSEvacuationNPC>(NpcClass, Floor.Location + FVector(0, 0, 95.f), Facing, Spawn))
				Placed.Add(Floor.Location);
		}
		UE_LOG(LogTemp, Display, TEXT("[YUFSTestNPC] Placed %d/%d %s inside fire data domain %s (%d beside level bags)"),
			Placed.Num(), Count, *NpcClass->GetName(), *Domain.ToString(), BagOwners);
		// Telemetry for unattended checks: what each NPC perceives and does, every 5 s.
		FTimerHandle Telemetry;
		const TWeakObjectPtr<UWorld> WeakWorld(World);
		World->GetTimerManager().SetTimer(Telemetry, FTimerDelegate::CreateLambda([WeakWorld]()
		{
			UWorld* W = WeakWorld.Get();
			if (!W) return;
			int32 Total = 0, Evacuated = 0, Moving = 0, Smoke = 0;
			for (TActorIterator<AYUFSEvacuationNPC> It(W); It; ++It)
			{
				const auto& O = It->GetLastObservation();
				++Total; Evacuated += It->IsHidden() ? 1 : 0;
				Moving += It->GetVelocity().Size2D() > 20.f ? 1 : 0;
				Smoke += O.SmokeDensityAtSelf > 0.05f ? 1 : 0;
				const auto* Bag = It->GetBelongingsRetrievalComponent();
				FString Direct = TEXT("-");
				for (TActorIterator<AYUFSBinaryManager> B(W); B; ++B)
				{
					const auto Snap = B->GetHazardSnapshot(B->GetCurrentFrame());
					const FVector Eye = It->GetPawnViewLocation();
					const auto S = Snap.Sample(Eye);
					Direct = FString::Printf(TEXT("%s cell=%s smoke=%.2f"),
						*StaticEnum<EYUFSHazardDataStatus>()->GetNameStringByValue(int64(S.Status)),
						*Snap.GridToWorld.InverseTransformPosition(Eye).ToCompactString(), S.Smoke);
					break;
				}
				const auto* Env = It->FindComponentByClass<UYUFSNpcEnvironmentInteraction>();
				const auto* Nav = It->GetNavigator();
				const auto* Local = It->GetLocalMovement();
				UE_LOG(LogTemp, Display, TEXT("[YUFSTestNPC] t=%.0f %s state=%s action=%s smoke=%.2f heat=%.2f risk=%.2f exposure=%.2f speed=%.0f hidden=%d bag=%s nav=%s local=%s env=%d/%d direct=[%s] pos=%s"),
					W->GetTimeSeconds(), *It->GetName(),
					*StaticEnum<EYUFSBehaviorState>()->GetNameStringByValue(int64(O.CurrentState)),
					*StaticEnum<EYUFSAction>()->GetNameStringByValue(int64(It->GetLastAction())),
					O.SmokeDensityAtSelf, FMath::Max(O.TemperatureAtSelf, O.NearbyHeatNormalized), O.RiskPerception,
					O.SmokeExposureAccumulated, It->GetVelocity().Size2D(), It->IsHidden() ? 1 : 0,
					Bag ? *StaticEnum<EYUFSBelongingsRetrievalPhase>()->GetNameStringByValue(int64(Bag->GetPhase())) : TEXT("-"),
					Nav ? *StaticEnum<EYUFSNavigationStatus>()->GetNameStringByValue(int64(Nav->GetNavigationStatus())) : TEXT("-"),
					Local ? *StaticEnum<EYUFSLocalMovementState>()->GetNameStringByValue(int64(Local->GetState())) : TEXT("-"),
					Env && Env->IsActive() ? 1 : 0, Env && Env->NeedsMovement() ? 1 : 0,
					*Direct, *It->GetActorLocation().ToCompactString());
			}
			int32 BinFrame = INDEX_NONE; FString Status = TEXT("NoBinary"); int32 PeakSmoke = 0, SmokeCells = 0;
			for (TActorIterator<AYUFSBinaryManager> It(W); It; ++It)
			{
				BinFrame = It->GetCurrentFrame();
				const auto Snapshot = It->GetHazardSnapshot(BinFrame);
				Status = StaticEnum<EYUFSHazardDataStatus>()->GetNameStringByValue(int64(Snapshot.Status));
				if (Snapshot.Grid.IsValid())
					for (const uint8 D : Snapshot.Grid->Density) { PeakSmoke = FMath::Max<int32>(PeakSmoke, D); SmokeCells += D >= 10; }
				break;
			}
			UE_LOG(LogTemp, Display, TEXT("[YUFSTestNPC] summary t=%.0f total=%d evacuated=%d moving=%d inSmoke=%d fireFrame=%d hazard=%s peakSmoke=%d smokeCells=%d"),
				W->GetTimeSeconds(), Total, Evacuated, Moving, Smoke, BinFrame, *Status, PeakSmoke, SmokeCells);
		}), 5.f, true);
		return Placed.Num();
	}
}

void UYUFSGameInstance::SetupBuildingInteractions(UWorld* World)
{
	if (!World || !World->IsGameWorld() || BuildingInteractionSetupWorld == World
		|| FParse::Param(FCommandLine::Get(), TEXT("YUFSNoBuildingInteractions"))
		|| (!bEnableBuildingInteractions
			&& !FParse::Param(FCommandLine::Get(), TEXT("YUFSBuildingInteractions")))) return;
	BuildingInteractionSetupWorld = World;
	{
		// Runs after the building's own spawner distributes its residents. Existing NPC
		// identities, classes, traits, meshes and placement remain untouched.
		FTimerHandle Handle;
		const TWeakObjectPtr<UWorld> SetupWorld(World);
		World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(this, [SetupWorld]()
		{
			UWorld* W = SetupWorld.Get();
			if (!W) return;
			int32 TestNpcCount = 0;
			if (FParse::Value(FCommandLine::Get(), TEXT("YUFSSpawnTestNPCs="), TestNpcCount) && TestNpcCount > 0)
				SpawnTestNPCs(W, FMath::Min(TestNpcCount, 100));
			// Optional interaction-only population parameters; JJW's PADM personality,
			// movement, response timing and user-authored NPC placement remain untouched.
			float TrainedFraction = 0.65f, LeaveBehindProbability = -1.f;
			bool bOverrideInteractionTraining = false;
			GConfig->GetBool(TEXT("YUFS.NpcInteraction"), TEXT("OverrideInteractionTrainingPopulation"), bOverrideInteractionTraining, GGameIni);
			GConfig->GetFloat(TEXT("YUFS.NpcInteraction"), TEXT("TrainedPopulationFraction"), TrainedFraction, GGameIni);
			GConfig->GetFloat(TEXT("YUFS.NpcInteraction"), TEXT("BelongingsLeaveBehindProbability"), LeaveBehindProbability, GGameIni);
			for (TActorIterator<AYUFSEvacuationNPC> It(W); It; ++It)
			{
				FRandomStream PopulationRng(20260908 ^ It->GetStableNPCId());
				const bool bTrained = PopulationRng.FRand() < FMath::Clamp(TrainedFraction, 0.f, 1.f);
				if (bOverrideInteractionTraining && It->GetHumanCognitionComponent())
					It->GetHumanCognitionComponent()->Traits.FireTraining = bTrained ? 0.85f : 0.2f;
				if (LeaveBehindProbability >= 0.f && It->GetBelongingsRetrievalComponent())
					It->GetBelongingsRetrievalComponent()->LeaveBehindProbability = FMath::Clamp(LeaveBehindProbability, 0.f, 1.f);
			}
			const bool bPreviewMode = FParse::Param(FCommandLine::Get(), TEXT("YUFSInteractionPreview"));
			AYUFSInteractionPreview* Preview = bPreviewMode ? W->SpawnActor<AYUFSInteractionPreview>() : nullptr;
			for (TActorIterator<AYUFSSimulationController> It(W); It; ++It)
			{
				if (bPreviewMode)
				{
					It->MaxSimDurationSeconds=3600.f;
					It->bEnableTimelineRecording=false;
				}
				// Preserve the configured countdown; do not shift FDS events to accelerate a demo.
				if (bPreviewMode)
					It->NotifyInteractionPreviewReady(Preview && Preview->IsReady());
				// The ordinary GUI remains in WaitingToStart so JJW's palette can
				// place, rotate and delete NPCs. Auto-start is a separate opt-in for
				// unattended tests, never a side effect of interaction setup.
				else if (FParse::Param(FCommandLine::Get(), TEXT("YUFSAutoStartSimulation")))
					It->StartSimulation();
			}
		}), 4.f, false);
	}
}

namespace
{
	const FString ResultsSaveSlotName = TEXT("YUFSRunResults");
	constexpr int32 ResultsSaveUserIndex = 0;
}

void UYUFSGameInstance::Init()
{
	Super::Init();
	LoadRunResultsFromDisk();
}

void UYUFSGameInstance::SaveRunResultsToDisk() const
{
	UYUFSRunResultsSaveGame* SaveGameObject = Cast<UYUFSRunResultsSaveGame>(
		UGameplayStatics::CreateSaveGameObject(UYUFSRunResultsSaveGame::StaticClass()));
	if (!SaveGameObject)
	{
		return;
	}

	SaveGameObject->SavedResults = LastRunResults;
	UGameplayStatics::SaveGameToSlot(SaveGameObject, ResultsSaveSlotName, ResultsSaveUserIndex);
}

void UYUFSGameInstance::LoadRunResultsFromDisk()
{
	if (!UGameplayStatics::DoesSaveGameExist(ResultsSaveSlotName, ResultsSaveUserIndex))
	{
		return;
	}

	if (UYUFSRunResultsSaveGame* SaveGameObject = Cast<UYUFSRunResultsSaveGame>(
		UGameplayStatics::LoadGameFromSlot(ResultsSaveSlotName, ResultsSaveUserIndex)))
	{
		LastRunResults = SaveGameObject->SavedResults;
		UE_LOG(LogTemp, Log, TEXT("[YUFS] 저장된 회차 기록 %d건을 불러왔습니다."), LastRunResults.Num());
	}
}

void UYUFSGameInstance::LaunchScenario(const FYUFSScenarioConfig& Config)
{
	FText Error;
	if (!Config.IsValid(Error))
	{
		UE_LOG(LogTemp, Error, TEXT("[YUFS] LaunchScenario 거부: %s"), *Error.ToString());
		return;
	}

	ActiveScenario = Config;
	bHasActiveScenario = true;

	const FString PackageName = Config.SimulationMap.ToSoftObjectPath().GetLongPackageName();
	if (PackageName.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("[YUFS] LaunchScenario: 맵 경로를 해석할 수 없습니다. (%s)"),
			*Config.SimulationMap.ToString());
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("[YUFS] 시나리오 시작 → %s (NPC %d)"),
		*PackageName, Config.NPCCount);

	UGameplayStatics::OpenLevel(this, FName(*PackageName));
}

bool UYUFSGameInstance::RelaunchLastScenario()
{
	if (!bHasActiveScenario)
	{
		return false;
	}

	LaunchScenario(ActiveScenario);
	return true;
}

void UYUFSGameInstance::StoreRunResults(const TArray<FSimRunResult>& Results)
{
	// LastRunResults는 세션을 넘어 누적되는 전체 기록이므로 덮어쓰지 않고 이어붙입니다.
	LastRunResults.Append(Results);
	SaveRunResultsToDisk();
}

bool UYUFSGameInstance::ReplayRun(const FSimRunResult& RunResult)
{
	// "현재" ActiveScenario가 아니라 그 회차가 실제로 썼던 설정 스냅샷을 그대로 씁니다.
	// (그 사이 다른 시나리오를 실행했어도 이 회차는 정확히 그때 설정으로 재현됩니다.)
	FText Error;
	if (!RunResult.ScenarioConfig.IsValid(Error))
	{
		UE_LOG(LogTemp, Warning, TEXT("[YUFS] ReplayRun 거부: 저장된 시나리오 설정이 유효하지 않습니다. %s"), *Error.ToString());
		return false;
	}

	// 배치 반복(1회로 제한)은 레벨 로드 후 SimulationController::StartSimulation()이 처리합니다.
	PendingReplaySeed = RunResult.RandomSeed;
	PendingReplayNPCTransforms = RunResult.InitialNPCTransforms;
	PendingReplayNPCClasses = RunResult.InitialNPCClasses;
	bHasPendingReplaySeed = true;

	LaunchScenario(RunResult.ScenarioConfig);
	return true;
}

void UYUFSGameInstance::ReturnToMainMenu()
{
	// ActiveScenario/LastRunResults는 "이어서 실행"과 결과 화면에서 쓸 수 있게 남겨둡니다.
	UE_LOG(LogTemp, Log, TEXT("[YUFS] 메인 메뉴로 복귀"));
	UGameplayStatics::OpenLevel(this, FName(TEXT("/Game/Maps/Lvl_MainMenu")));
}
