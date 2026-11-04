#include "Debug/YUFSMapDiagnostics.h"

#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"

FYUFSMapLocationProbe UYUFSMapDiagnostics::ProbeLocation(UObject* WorldContextObject,
    FVector ApproximateFeet, TSubclassOf<ACharacter> CharacterClass, float VerticalSearchCm)
{
    FYUFSMapLocationProbe Result;
    UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
    const ACharacter* Template = CharacterClass ? CharacterClass->GetDefaultObject<ACharacter>() : nullptr;
    if (!World || !Template || ApproximateFeet.ContainsNaN() || !FMath::IsFinite(VerticalSearchCm))
    {
        Result.Reason = TEXT("InvalidWorldCharacterOrPoint");
        return Result;
    }
    const auto* Capsule = Template->GetCapsuleComponent();
    const auto* Movement = Template->GetCharacterMovement();
    const float Search = FMath::Clamp(VerticalSearchCm, 1.f, 150.f);
    FHitResult Floor;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(FinalMapFloor), false);
    const bool bHit = World->LineTraceSingleByChannel(Floor, ApproximateFeet + FVector(0,0,Search),
        ApproximateFeet - FVector(0,0,Search), ECC_Visibility, Query);
    Result.FloorLocation = Floor.ImpactPoint;
    Result.FloorActor = GetNameSafe(Floor.GetActor());
    if (!bHit || !Movement->IsWalkable(Floor))
    {
        Result.Reason = bHit ? FString::Printf(TEXT("UnwalkablePhysicalFloor normalZ=%.4f minimumZ=%.4f penetrating=%d actor=%s"),
            Floor.ImpactNormal.Z,Movement->GetWalkableFloorZ(),Floor.bStartPenetrating,*Result.FloorActor)
            : TEXT("NoPhysicalFloorHit");
        return Result;
    }
    Result.bSupported = true;
    Result.FloorLocation = Floor.ImpactPoint;
    Result.FloorActor = GetNameSafe(Floor.GetActor());
    const float Radius = Capsule->GetScaledCapsuleRadius();
    const float Half = Capsule->GetScaledCapsuleHalfHeight();
    Result.bBodyClear = !World->OverlapBlockingTestByChannel(Result.FloorLocation + FVector(0,0,Half+2.f),
        FQuat::Identity, Capsule->GetCollisionObjectType(), FCollisionShape::MakeCapsule(Radius,Half), Query,
        FCollisionResponseParams(Capsule->GetCollisionResponseToChannels()));
    auto* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
    FNavLocation Projected;
    if (Nav && Nav->ProjectPointToNavigation(Result.FloorLocation,Projected,FVector(35,35,Search),
        &Movement->GetNavAgentPropertiesRef()))
    {
        Result.NavigationLocation = Projected.Location;
        Result.bOnNavigation = FVector::DistSquared2D(Projected.Location,Result.FloorLocation) <= FMath::Square(10.f)
            && FMath::Abs(Projected.Location.Z-Result.FloorLocation.Z) <= 30.f;
    }
    Result.Reason = !Result.bBodyClear ? TEXT("PhysicalBodyBlocked")
        : (!Result.bOnNavigation ? TEXT("NoMatchingNavigationFloor") : TEXT("Supported"));
    return Result;
}

FYUFSMapRouteProbe UYUFSMapDiagnostics::ProbeRoute(UObject* WorldContextObject, FVector StartFeet, FVector EndFeet)
{
    FYUFSMapRouteProbe Result;
    if (StartFeet.ContainsNaN() || EndFeet.ContainsNaN()) return Result;
    const auto* Path = UNavigationSystemV1::FindPathToLocationSynchronously(WorldContextObject,StartFeet,EndFeet);
    if (Path)
    {
        Result.bComplete = Path->IsValid() && !Path->IsPartial();
        Result.FloorPoints = Path->PathPoints;
    }
    return Result;
}
