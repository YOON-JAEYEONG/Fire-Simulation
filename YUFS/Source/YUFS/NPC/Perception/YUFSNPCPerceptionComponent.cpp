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
	return Snapshot;
}
void UYUFSNPCPerceptionComponent::UpdateFromSnapshot(const FYUFSHazardSnapshot& Snapshot, float Now)
{
	CachedSmokeDensity = CachedTemperature = CachedSmokeInFrontNormalized = CachedSmokeAboveNormalized = 0.f;
	CachedHeatInSight = CachedNearbyHeat = CachedRiskLevel = 0.f;
	DataStatus = Snapshot.Status;
	if (!Config || !GetOwner() || !GetWorld()) return;
	if (Snapshot.Grid && (LastGridDimensions != Snapshot.Grid->Dimensions || !LastGridTransform.Equals(Snapshot.GridToWorld) || Snapshot.Frame < LastFrame))
	{
		KnownCells.Reset(); LastObservedAt.Reset();
		LastGridDimensions = Snapshot.Grid->Dimensions; LastGridTransform = Snapshot.GridToWorld;
	}
	LastFrame = Snapshot.Frame;
	for (auto It = LastObservedAt.CreateIterator(); It; ++It)
	{
		if (Now - It.Value() > FMath::Max(1.f, HazardMemorySeconds)) { KnownCells.Remove(It.Key()); It.RemoveCurrent(); }
	}
	if (Snapshot.Status == EYUFSHazardDataStatus::Ready)
	{
		ACharacter* Character = Cast<ACharacter>(GetOwner());
		const FVector Pos = GetOwner()->GetActorLocation();
		const FVector Eye = Character ? Character->GetPawnViewLocation() : Pos;
		const float HalfHeight = Character ? Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 90.f;
		const FVector Feet = Pos - FVector(0,0,HalfHeight);
		FCollisionQueryParams Params(SCENE_QUERY_STAT(YUFSHazardPerception), false, GetOwner());
		auto Observe = [&](const FVector& P)
		{
			const auto Value = Snapshot.Sample(P);
			const int32 Index = Snapshot.CellIndex(P);
			if (Value.Status == EYUFSHazardDataStatus::Ready && Index != INDEX_NONE)
			{
				// Clear observations erase obsolete hazards. Store only positive cells to bound copies.
				if (Value.Smoke > 0.01f || Value.Heat > 0.01f)
				{
					if (KnownCells.Num() < 8192 || KnownCells.Contains(Index)) { KnownCells.Add(Index,Value); LastObservedAt.Add(Index,Now); }
				}
				else { KnownCells.Remove(Index); LastObservedAt.Remove(Index); }
			}
			return Value;
		};
		const auto Self = Observe(Eye);
		CachedSmokeDensity = Self.Smoke; CachedTemperature = Self.Heat;
		for (float H : {20.f, HalfHeight, 120.f})
		{
			const FVector P = Feet + FVector(0,0,H);
			if (!GetWorld()->LineTraceTestByChannel(Eye,P,ECC_Visibility,Params))
				CachedNearbyHeat = FMath::Max(CachedNearbyHeat,Observe(P).Heat);
		}
		auto Scan = [&](FVector Origin, FVector Direction, float Range, float& Smoke, float& Heat)
		{
			Range = FMath::Clamp(Range, 1.f, 3000.f);
			FHitResult Hit;
			const bool bHit = GetWorld()->LineTraceSingleByChannel(Hit,Origin,Origin+Direction*Range,ECC_Visibility,Params);
			const float Visible = bHit ? FMath::Max(0.f,Hit.Distance-Config->OcclusionSampleMarginCm) : Range;
			const int32 Count = FMath::Clamp(FMath::CeilToInt(Range / FMath::Clamp(Config->MaxProbeStepCm,10.f,80.f)),1,300);
			for (int32 I=1; I<=Count; ++I)
			{
				const float D = Range*I/Count;
				if (D > Visible) break;
				const auto V = Observe(Origin + Direction*D);
				// Nearby cues are stronger; distance never turns a high reading into zero.
				const float Weight = FMath::Lerp(1.f,0.65f,D/Range);
				Smoke = FMath::Max(Smoke,V.Smoke*Weight); Heat = FMath::Max(Heat,V.Heat*Weight);
			}
			if (Config->bDrawVisionDebug) DrawDebugLine(GetWorld(),Origin,Origin+Direction*Visible,FColor::Cyan,false,0.21f);
		};
		const float Yaw = GetOwner()->GetActorRotation().Yaw;
		const int32 Rays = FMath::Clamp(Config->VisionRayCount,1,15);
		for (int32 I=0; I<Rays; ++I)
		{
			const float Offset = Rays==1 ? 0.f : FMath::Lerp(-Config->FieldOfViewDegrees/2,Config->FieldOfViewDegrees/2,float(I)/(Rays-1));
			Scan(Eye,FRotator(0,Yaw+Offset,0).Vector(),Config->VisionRange,CachedSmokeInFrontNormalized,CachedHeatInSight);
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
			Scan(Feet+FVector(0,0,60),FRotator(0,I*45.f,0).Vector(),Config->NearHeatRange,UnusedSmoke,CachedNearbyHeat);
		CachedRiskLevel = ComputeRiskLevel(CachedSmokeDensity,FMath::Max(CachedTemperature,CachedNearbyHeat));
	}
	PublishedKnowledge = MakeShared<const TMap<int32,FYUFSHazardSample>,ESPMode::ThreadSafe>(KnownCells);
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
