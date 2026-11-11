#include "NPC/Perception/YUFSNPCPerceptionComponent.h"
#include "DrawDebugHelpers.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Fire/YUFSBinaryManager.h"
#include "GameFramework/Character.h"
#include "Components/CapsuleComponent.h"

UYUFSNPCPerceptionComponent::UYUFSNPCPerceptionComponent() { PrimaryComponentTick.bCanEverTick = false; }
void UYUFSNPCPerceptionComponent::BeginPlay()
{
	Super::BeginPlay();
	if (!Config) Config = NewObject<UYUFSPerceptionConfig>(this);
	for (TActorIterator<AYUFSBinaryManager> It(GetWorld()); It; ++It) { BinaryManager = *It; break; }
}
void UYUFSNPCPerceptionComponent::UpdatePerception(int32 Frame)
{
	UpdateFromSnapshot(BinaryManager ? BinaryManager->GetHazardSnapshot(Frame) : FYUFSHazardSnapshot(), GetWorld()->GetTimeSeconds());
}
FYUFSHazardSnapshot UYUFSNPCPerceptionComponent::RestrictToKnowledge(FYUFSHazardSnapshot Snapshot) const
{
	Snapshot.KnownCells = PublishedKnowledge.IsValid() ? PublishedKnowledge
		: MakeShared<const TMap<int32, FYUFSHazardSample>, ESPMode::ThreadSafe>();
	Snapshot.ObservedCells = PublishedObservedCells.IsValid() ? PublishedObservedCells
		: MakeShared<const TSet<int32>, ESPMode::ThreadSafe>();
	return Snapshot;
}
FYUFSSmokeSightResult UYUFSNPCPerceptionComponent::TraceSight(const FYUFSHazardSnapshot& Snapshot,
	const FVector& Origin, const FVector& Target, bool bOptical,
	TFunctionRef<void(const FVector&, const FYUFSHazardSample&, float)> OnVisibleSample) const
{
	FYUFSSmokeSightResult Result;
	if (!Config || !GetOwner() || !GetWorld() || Origin.ContainsNaN() || Target.ContainsNaN()) return Result;
	const float Range=FVector::Distance(Origin,Target);
	Result.bCompleteSmokeData=Snapshot.Status==EYUFSHazardDataStatus::Ready && Snapshot.Grid.IsValid();
	if (Range<KINDA_SMALL_NUMBER) { Result.bReachedTarget=true; return Result; }
	const FVector Direction=(Target-Origin)/Range;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(YUFSSmokeSight),false,GetOwner());
	FHitResult Hit;
	const bool bHit=GetWorld()->LineTraceSingleByChannel(Hit,Origin,Target,ECC_Visibility,Params);
	const float Limit=bHit ? FMath::Max(0.f,Hit.Distance-FMath::Max(0.f,Config->OcclusionSampleMarginCm)) : Range;
	float Step=FMath::Clamp(Config->MaxProbeStepCm,10.f,80.f);
	if (Snapshot.Grid.IsValid() && Snapshot.GridToWorld.IsValid())
		Step=FMath::Min(Step,FMath::Clamp(float(Snapshot.GridToWorld.GetScale3D().GetAbsMin())*.5f,5.f,80.f));
	const int32 Count=FMath::Clamp(FMath::CeilToInt(Range/Step),1,1024);
	const float Attenuation=bOptical ? FMath::Clamp(Config->SmokeSightAttenuationPerMeter,0.f,10.f) : 0.f;
	const float Cutoff=FMath::Clamp(Config->MinimumSightTransmission,.01f,.95f);
	for (int32 I=0; I<Count; ++I)
	{
		const float Start=Range*I/Count, End=FMath::Min(Limit,Range*(I+1)/Count);
		if (End<=Start) break;
		const FVector Point=Origin+Direction*((Start+End)*.5f);
		const auto Value=Snapshot.Sample(Point);
		Result.bCompleteSmokeData &= Value.Status==EYUFSHazardDataStatus::Ready
			&& (!Snapshot.KnownCells.IsValid() || Snapshot.IsObservedAt(Point));
		const float Dose=Value.Status==EYUFSHazardDataStatus::Ready ? Value.Smoke*Attenuation*(End-Start)/100.f : 0.f;
		const float NextTransmission=Result.Transmission*FMath::Exp(-Dose);
		if (NextTransmission<Cutoff)
		{
			const float Fraction=Dose>SMALL_NUMBER ? FMath::Clamp(FMath::Loge(Result.Transmission/Cutoff)/Dose,0.f,1.f) : 0.f;
			Result.VisibleDistanceCm=FMath::Lerp(Start,End,Fraction);
			Result.Transmission=Cutoff;
			return Result; // Never observe the cell behind the visual cutoff or erase its old memory.
		}
		Result.Transmission=NextTransmission;
		Result.VisibleDistanceCm=End;
		OnVisibleSample(Point,Value,Result.Transmission);
		if (End>=Limit) break;
	}
	Result.bReachedTarget=!bHit && Result.VisibleDistanceCm>=Range-KINDA_SMALL_NUMBER;
	return Result;
}

FYUFSSmokeSightResult UYUFSNPCPerceptionComponent::EvaluateSightToPoint(
	const FYUFSHazardSnapshot& Snapshot, const FVector& WorldPos) const
{
	if (!Config || !GetOwner() || !GetWorld() || WorldPos.ContainsNaN()) return {};
	const ACharacter* Character=Cast<ACharacter>(GetOwner());
	const FVector Eye=Character ? Character->GetPawnViewLocation() : GetOwner()->GetActorLocation();
	const FVector Delta=WorldPos-Eye;
	if (Delta.SizeSquared()>FMath::Square(FMath::Clamp(Config->VisionRange,1.f,3000.f))) return {};
	const FVector Flat=Delta.GetSafeNormal2D();
	const float HalfFov=FMath::Clamp(Config->FieldOfViewDegrees,1.f,360.f)*.5f;
	if (!Flat.IsNearlyZero() && FVector::DotProduct(GetOwner()->GetActorForwardVector().GetSafeNormal2D(),Flat)
		<FMath::Cos(FMath::DegreesToRadians(HalfFov))) return {};
	return TraceSight(Snapshot,Eye,WorldPos,true,[](const FVector&,const FYUFSHazardSample&,float){});
}

void UYUFSNPCPerceptionComponent::UpdateFromSnapshot(const FYUFSHazardSnapshot& Snapshot, float Now)
{
	CachedSmokeDensity = CachedTemperature = CachedSmokeInFrontNormalized = CachedSmokeAboveNormalized = 0.f;
	CachedHeatInSight = CachedNearbyHeat = CachedRiskLevel = 0.f;
	bSelfHazardSampleAvailable = false;
	CenterSight = {};
	DataStatus = Snapshot.Status;
	if (!Config || !GetOwner() || !GetWorld()) return;
	if (Snapshot.Grid && (LastGridDimensions != Snapshot.Grid->Dimensions || !LastGridTransform.Equals(Snapshot.GridToWorld) || Snapshot.Frame < LastFrame))
	{
		KnownCells.Reset(); ObservedCells.Reset(); LastObservedAt.Reset();
		LastGridDimensions = Snapshot.Grid->Dimensions; LastGridTransform = Snapshot.GridToWorld;
	}
	LastFrame = Snapshot.Frame;
	for (auto It = LastObservedAt.CreateIterator(); It; ++It)
	{
		if (Now - It.Value() > FMath::Max(1.f, HazardMemorySeconds)) { KnownCells.Remove(It.Key()); ObservedCells.Remove(It.Key()); It.RemoveCurrent(); }
	}
	if (Snapshot.Status == EYUFSHazardDataStatus::Ready)
	{
		ACharacter* Character = Cast<ACharacter>(GetOwner());
		const FVector Pos = GetOwner()->GetActorLocation();
		const FVector Eye = Character ? Character->GetPawnViewLocation() : Pos;
		const float HalfHeight = Character ? Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 90.f;
		const FVector Feet = Pos - FVector(0,0,HalfHeight);
		FCollisionQueryParams Params(SCENE_QUERY_STAT(YUFSHazardPerception), false, GetOwner());
		auto Observe = [&](const FVector& P, const FYUFSHazardSample* AlreadySampled=nullptr)
		{
			const auto Value = AlreadySampled ? *AlreadySampled : Snapshot.Sample(P);
			const int32 Index = Snapshot.CellIndex(P);
			if (Value.Status == EYUFSHazardDataStatus::Ready && Index != INDEX_NONE)
			{
				if (ObservedCells.Num() < 8192 || ObservedCells.Contains(Index)) { ObservedCells.Add(Index); LastObservedAt.Add(Index, Now); }
				// Clear observations erase obsolete hazards. Store only positive cells to bound copies.
				if (Value.Smoke > 0.01f || Value.Heat > 0.01f)
				{
					if (KnownCells.Num() < 8192 || KnownCells.Contains(Index)) { KnownCells.Add(Index,Value); LastObservedAt.Add(Index,Now); }
				}
				else { KnownCells.Remove(Index); }
			}
			return Value;
		};
		const auto Self = Observe(Eye);
		bSelfHazardSampleAvailable = Self.Status == EYUFSHazardDataStatus::Ready;
		CachedSmokeDensity = Self.Smoke; CachedTemperature = Self.Heat;
		const float BodyProbeHeight = Character && Character->IsCrouched() ? HalfHeight * 2.f : 120.f;
		for (float H : {20.f, HalfHeight, BodyProbeHeight})
		{
			const FVector P = Feet + FVector(0,0,H);
			if (!GetWorld()->LineTraceTestByChannel(Eye,P,ECC_Visibility,Params))
				CachedNearbyHeat = FMath::Max(CachedNearbyHeat,Observe(P).Heat);
		}
		auto Scan = [&](FVector Origin, FVector Direction, float Range, float& Smoke, float& Heat, bool bOptical=true)
		{
			Range = FMath::Clamp(Range, 1.f, 3000.f);
			const auto Sight=TraceSight(Snapshot,Origin,Origin+Direction*Range,bOptical,
			[&](const FVector& Point,const FYUFSHazardSample& V,float Transmission)
			{
				Observe(Point,&V);
				const float Weight=FMath::Lerp(1.f,.65f,FVector::Distance(Origin,Point)/Range);
				Smoke=FMath::Max(Smoke,V.Smoke*Weight);
				Heat=FMath::Max(Heat,V.Heat*Weight*(bOptical?Transmission:1.f));
			});
			if (Config->bDrawVisionDebug) DrawDebugLine(GetWorld(),Origin,Origin+Direction*Sight.VisibleDistanceCm,FColor::Cyan,false,0.21f);
			return Sight;
		};
		const float Yaw = GetOwner()->GetActorRotation().Yaw;
		const int32 Rays = FMath::Clamp(Config->VisionRayCount,1,15);
		for (int32 I=0; I<Rays; ++I)
		{
			const float Offset = Rays==1 ? 0.f : FMath::Lerp(-Config->FieldOfViewDegrees/2,Config->FieldOfViewDegrees/2,float(I)/(Rays-1));
			const auto HorizontalSight=Scan(Eye,FRotator(0,Yaw+Offset,0).Vector(),Config->VisionRange,CachedSmokeInFrontNormalized,CachedHeatInSight);
			if (I==Rays/2) CenterSight=HorizontalSight;
			Scan(Eye,FRotator(-25,Yaw+Offset,0).Vector(),Config->VisionRange,CachedSmokeInFrontNormalized,CachedHeatInSight);
		}
		for (float Pitch : {Config->UpperVisionPitchLowDegrees,Config->UpperVisionPitchHighDegrees})
		{
			const int32 Count = FMath::Clamp(Config->UpperVisionYawRayCount,1,9);
			for (int32 I=0; I<Count; ++I)
			{
				float Offset = Count==1 ? 0.f : FMath::Lerp(-Config->UpperVisionYawHalfSpreadDegrees,Config->UpperVisionYawHalfSpreadDegrees,float(I)/(Count-1));
				Scan(Eye,FRotator(Pitch,Yaw+Offset,0).Vector(),Config->VisionRange,CachedSmokeAboveNormalized,CachedHeatInSight);
			}
		}
		Scan(Eye,FRotator(Config->OverheadProbePitchDegrees,Yaw,0).Vector(),Config->OverheadProbeRange,CachedSmokeAboveNormalized,CachedHeatInSight);
		// Local heat exposure is omnidirectional, but probes stop at walls and floor slabs.
		float UnusedSmoke = 0.f;
		for (int32 I=0; I<8; ++I)
			Scan(Feet+FVector(0,0,60),FRotator(0,I*45.f,0).Vector(),Config->NearHeatRange,UnusedSmoke,CachedNearbyHeat,false);
		CachedRiskLevel = ComputeRiskLevel(CachedSmokeDensity,FMath::Max(CachedTemperature,CachedNearbyHeat));
	}
	PublishedKnowledge = MakeShared<const TMap<int32,FYUFSHazardSample>,ESPMode::ThreadSafe>(KnownCells);
	PublishedObservedCells = MakeShared<const TSet<int32>, ESPMode::ThreadSafe>(ObservedCells);
}
float UYUFSNPCPerceptionComponent::SampleSmokeAtPoint(FVector P, int32 Frame) const
{
	return BinaryManager ? BinaryManager->GetHazardSnapshot(Frame).Sample(P).Smoke : 0.f;
}

void UYUFSNPCPerceptionComponent::ReceiveHazardReport(const UYUFSNPCPerceptionComponent& Other)
{
	if (LastGridDimensions!=Other.LastGridDimensions || !LastGridTransform.Equals(Other.LastGridTransform)) return;
	int32 Count=0;
	for (const auto& Pair:Other.KnownCells)
	{
		if (++Count>256 || KnownCells.Num()>=8192) break;
		const float* At=Other.LastObservedAt.Find(Pair.Key);
		if (!At) continue;
		const float* Existing=LastObservedAt.Find(Pair.Key);
		if (!Existing || *Existing < *At) { KnownCells.Add(Pair.Key,Pair.Value); LastObservedAt.Add(Pair.Key,*At); }
	}
}

void UYUFSNPCPerceptionComponent::ResetKnowledge()
{
	KnownCells.Reset();
	ObservedCells.Reset();
	LastObservedAt.Reset();
	PublishedKnowledge = MakeShared<const TMap<int32, FYUFSHazardSample>, ESPMode::ThreadSafe>();
	PublishedObservedCells = MakeShared<const TSet<int32>, ESPMode::ThreadSafe>();
	LastGridTransform = FTransform::Identity;
	LastGridDimensions = FIntVector::ZeroValue;
	LastFrame = INDEX_NONE;
	bSelfHazardSampleAvailable = false;
	CachedSmokeDensity = CachedTemperature = CachedSmokeInFrontNormalized = CachedSmokeAboveNormalized = 0.f;
	CachedHeatInSight = CachedNearbyHeat = CachedRiskLevel = 0.f;
	DataStatus = EYUFSHazardDataStatus::MissingData;
	CenterSight = {};
}

bool UYUFSNPCPerceptionComponent::SampleObservedHazard(
	const FVector& WorldPos, int32 Frame, float& OutSmoke, float& OutHeat)
{
	OutSmoke = OutHeat = 0.f;
	if (!IsValid(BinaryManager) || !Config || !GetOwner() || !GetWorld() || WorldPos.ContainsNaN()) return false;
	const FYUFSHazardSnapshot Snapshot = BinaryManager->GetHazardSnapshot(Frame);
	return SampleObservedHazardFromSnapshot(Snapshot,WorldPos,GetWorld()->GetTimeSeconds(),OutSmoke,OutHeat);
}

bool UYUFSNPCPerceptionComponent::SampleObservedHazardFromSnapshot(const FYUFSHazardSnapshot& Snapshot,
	const FVector& WorldPos, float Now, float& OutSmoke, float& OutHeat)
{
	OutSmoke=OutHeat=0.f;
	if (Snapshot.Status != EYUFSHazardDataStatus::Ready || !Snapshot.Grid) return false;
	const auto Sight=EvaluateSightToPoint(Snapshot,WorldPos);
	if (!Sight.bReachedTarget || !Sight.bCompleteSmokeData) return false;
	const FYUFSHazardSample Value = Snapshot.Sample(WorldPos);
	const int32 Index = Snapshot.CellIndex(WorldPos);
	if (Value.Status != EYUFSHazardDataStatus::Ready || Index == INDEX_NONE) return false;
	if (LastGridDimensions != Snapshot.Grid->Dimensions || !LastGridTransform.Equals(Snapshot.GridToWorld) || Snapshot.Frame < LastFrame)
	{
		ResetKnowledge();
		LastGridDimensions = Snapshot.Grid->Dimensions;
		LastGridTransform = Snapshot.GridToWorld;
	}
	LastFrame = Snapshot.Frame;
	if (ObservedCells.Num() < 8192 || ObservedCells.Contains(Index)) { ObservedCells.Add(Index); LastObservedAt.Add(Index, Now); }
	if (Value.Smoke > 0.01f || Value.Heat > 0.01f)
	{
		if (KnownCells.Num() < 8192 || KnownCells.Contains(Index))
		{
			KnownCells.Add(Index, Value);
			LastObservedAt.Add(Index, Now);
		}
	}
	else
	{
		KnownCells.Remove(Index);
	}
	PublishedKnowledge = MakeShared<const TMap<int32, FYUFSHazardSample>, ESPMode::ThreadSafe>(KnownCells);
	PublishedObservedCells = MakeShared<const TSet<int32>, ESPMode::ThreadSafe>(ObservedCells);
	OutSmoke = Value.Smoke;
	OutHeat = Value.Heat;
	return true;
}
