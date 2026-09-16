#include "Debug/YUFSExtinguisherDemoDirector.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "EngineUtils.h"
#include "Fire/YUFSFireExtinguisher.h"
#include "Fire/YUFSSuppressibleFireSource.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "NPC/Integration/YUFSTeamIntegrationComponent.h"
#include "NPC/YUFSEvacuationNPC.h"
#include "UObject/ConstructorHelpers.h"
#include "UnrealClient.h"

namespace
{
constexpr int32 DemoNpcId = 9001;

FName ResolveRightHandName(const USkeletalMeshComponent* Mesh)
{
	if (!Mesh)
	{
		return NAME_None;
	}

	// The existing building character is Mixamo-based, while Manny uses
	// hand_r. Resolve both families so interaction props do not depend on which
	// visual implementation the level spawner selected.
	static const FName Candidates[] = {
		TEXT("hand_r"),
		TEXT("RightHand"),
		TEXT("mixamorig_RightHand")
	};
	for (const FName Candidate : Candidates)
	{
		if (Mesh->DoesSocketExist(Candidate) || Mesh->GetBoneIndex(Candidate) != INDEX_NONE)
		{
			return Candidate;
		}
	}
	return NAME_None;
}
}

AYUFSExtinguisherDemoDirector::AYUFSExtinguisherDemoDirector()
{
	PrimaryActorTick.bCanEverTick = true;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeAsset(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderAsset(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));

	StageFloor = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("StageFloor"));
	StageFloor->SetupAttachment(SceneRoot);
	StageFloor->SetStaticMesh(CubeAsset.Object);
	StageFloor->SetRelativeLocation(FVector(0.f, 0.f, -6.f));
	StageFloor->SetRelativeScale3D(FVector(11.f, 6.5f, 0.12f));
	StageFloor->SetCollisionProfileName(TEXT("BlockAll"));

	SprayBeam = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SprayBeam"));
	SprayBeam->SetupAttachment(SceneRoot);
	SprayBeam->SetStaticMesh(CylinderAsset.Object);
	SprayBeam->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SprayBeam->SetCastShadow(false);
	SprayBeam->SetVisibility(false);

	DemoTitle = CreateDefaultSubobject<UTextRenderComponent>(TEXT("DemoTitle"));
	DemoTitle->SetupAttachment(SceneRoot);
	DemoTitle->SetRelativeLocation(FVector(0.f, 0.f, 285.f));
	DemoTitle->SetRelativeRotation(FRotator(0.f, -90.f, 0.f));
	DemoTitle->SetHorizontalAlignment(EHTA_Center);
	DemoTitle->SetWorldSize(34.f);
	DemoTitle->SetTextRenderColor(FColor(90, 220, 255));
	DemoTitle->SetText(FText::FromString(TEXT("YUFS NPC EXTINGUISHER INTERACTION\nDECISION -> ACQUIRE -> SUPPRESS -> FEEDBACK")));
}

void AYUFSExtinguisherDemoDirector::BeginPlay()
{
	Super::BeginPlay();
	// Retained for asset compatibility only. Never spawn a parallel fire scenario.
	SetActorTickEnabled(false);
	UE_LOG(LogTemp, Warning, TEXT("[NPCSuppression] Synthetic-fire demo is disabled."));
}

void AYUFSExtinguisherDemoDirector::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	PhaseElapsedSeconds += DeltaTime;
	UpdateCamera();

	if (!IsValid(DemoNpc) || !IsValid(Extinguisher) || !IsValid(FireSource))
	{
		EnterPhase(EYUFSExtinguisherDemoPhase::Failed, TEXT("Demo actor became invalid"));
		UpdateScreenStatus();
		return;
	}

	if (Extinguisher->GetOwnerActor() == DemoNpc)
	{
		UpdateHeldExtinguisherTransform();
	}

	switch (Phase)
	{
	case EYUFSExtinguisherDemoPhase::MovingToExtinguisher:
		MoveNpcToward(ExtinguisherUsePoint, DeltaTime, 105.f);
		if (FVector::DistSquared(DemoNpc->GetActorLocation(), ExtinguisherUsePoint) < FMath::Square(8.f))
		{
			AcquireExtinguisher();
		}
		break;

	case EYUFSExtinguisherDemoPhase::MovingToFire:
		MoveNpcToward(FireAttackPoint, DeltaTime, 115.f);
		if (FVector::DistSquared(DemoNpc->GetActorLocation(), FireAttackPoint) < FMath::Square(8.f))
		{
			BeginSuppression();
		}
		break;

	case EYUFSExtinguisherDemoPhase::Suppressing:
	{
		const float SweepYaw = FMath::Sin(PhaseElapsedSeconds * 2.25f) * 5.f;
		FRotator AimRotation = (FireSource->GetActorLocation() - DemoNpc->GetActorLocation()).Rotation();
		AimRotation.Pitch = 0.f;
		AimRotation.Roll = 0.f;
		AimRotation.Yaw += SweepYaw;
		DemoNpc->SetActorRotation(AimRotation);
		UpdateHeldExtinguisherTransform();
		const float Consumed = Extinguisher->ConsumeAgent(DemoNpc, DeltaTime * 0.75f);
		FireSource->ApplySuppression(Consumed * 0.28f);
		UpdateSprayBeam();
		if (!bCaptureRequested
			&& PhaseElapsedSeconds >= 1.f
			&& FParse::Param(FCommandLine::Get(), TEXT("YUFSExtinguisherCapture")))
		{
			const FName RightHandName = ResolveRightHandName(DemoNpc->GetMesh());
			const FVector HandLocation = DemoNpc->GetMesh() && !RightHandName.IsNone()
				? DemoNpc->GetMesh()->GetSocketLocation(RightHandName)
				: FVector::ZeroVector;
			UE_LOG(
				LogTemp,
				Display,
				TEXT("[YUFSExtinguisherDemo] Capture transforms NPC=%s HandR=%s Ext=%s Nozzle=%s Fire=%s"),
				*DemoNpc->GetActorLocation().ToCompactString(),
				*HandLocation.ToCompactString(),
				*Extinguisher->GetActorLocation().ToCompactString(),
				*Extinguisher->GetNozzleWorldLocation().ToCompactString(),
				*FireSource->GetActorLocation().ToCompactString());
			const FString ScreenshotPath = FPaths::Combine(
				FPaths::ProjectSavedDir(),
				TEXT("Screenshots/Windows/YUFS_Extinguisher_Unified.png"));
			FScreenshotRequest::RequestScreenshot(ScreenshotPath, true, false);
			bCaptureRequested = true;
		}
		if (FireSource->IsExtinguished())
		{
			FinishSuppression();
		}
		else if (Extinguisher->GetExtinguisherState() == EYUFSFireExtinguisherState::Empty)
		{
			SprayBeam->SetVisibility(false);
			EnterPhase(EYUFSExtinguisherDemoPhase::Failed, TEXT("Agent depleted before suppression completed"));
		}
		break;
	}

	case EYUFSExtinguisherDemoPhase::Succeeded:
		if (PhaseElapsedSeconds >= 4.f)
		{
			SetupDemo();
		}
		break;

	default:
		break;
	}

	UpdateScreenStatus();
}

void AYUFSExtinguisherDemoDirector::SetupDemo()
{
	if (IsValid(DemoNpc)) DemoNpc->Destroy();
	if (IsValid(Extinguisher)) Extinguisher->Destroy();
	if (IsValid(FireSource)) FireSource->Destroy();
	if (IsValid(DemoCamera)) DemoCamera->Destroy();
	DemoNpc = nullptr;
	Extinguisher = nullptr;
	FireSource = nullptr;
	DemoCamera = nullptr;
	TeamIntegration = nullptr;
	bCameraAssigned = false;
	LastNavigationDirective = FYUFSNavigationDirective{};
	LastMotionDirective = FYUFSMotionDirective{};
	LastInteractionDirective = FYUFSInteractionDirective{};
	bCaptureRequested = false;

	const APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0);
	const FVector PlayerLocation = PlayerPawn ? PlayerPawn->GetActorLocation() : FVector::ZeroVector;
	bBuildingMode = UGameplayStatics::GetCurrentLevelName(this, true).Contains(TEXT("Prototype"));
	StageOrigin = PlayerLocation + FVector(0.f, 0.f, 650.f);
	AYUFSEvacuationNPC* VisualTemplateNpc = nullptr;
	if (bBuildingMode)
	{
		bool bFoundIndoorAnchor = false;
		for (TActorIterator<AYUFSEvacuationNPC> It(GetWorld()); It; ++It)
		{
			if (!IsValid(*It) || It->IsHidden())
			{
				continue;
			}
			const float HalfHeight = It->GetCapsuleComponent()
				? It->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()
				: 88.f;
			StageOrigin = It->GetActorLocation() - FVector(0.f, 0.f, HalfHeight);
			VisualTemplateNpc = *It;
			bFoundIndoorAnchor = true;
			break;
		}
		if (!bFoundIndoorAnchor && PlayerPawn)
		{
			StageOrigin = PlayerLocation - FVector(0.f, 0.f, 90.f);
		}
	}
	SetActorLocation(StageOrigin);
	DemoTitle->SetVisibility(false);
	StageFloor->SetVisibility(!bBuildingMode);
	StageFloor->SetCollisionEnabled(bBuildingMode ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryAndPhysics);

	FloorMaterial = StageFloor->CreateAndSetMaterialInstanceDynamic(0);
	if (FloorMaterial)
	{
		FloorMaterial->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.018f, 0.025f, 0.045f));
	}
	SprayMaterial = SprayBeam->CreateAndSetMaterialInstanceDynamic(0);
	if (SprayMaterial)
	{
		SprayMaterial->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.72f, 0.92f, 1.f));
	}

	UClass* NpcClass = VisualTemplateNpc ? VisualTemplateNpc->GetClass() : nullptr;
	if (!NpcClass)
	{
		NpcClass = StaticLoadClass(
			AYUFSEvacuationNPC::StaticClass(),
			nullptr,
			TEXT("/Game/Blueprint/BP_YUFSRLEvacuationNPC.BP_YUFSRLEvacuationNPC_C"));
	}
	if (!NpcClass)
	{
		NpcClass = AYUFSEvacuationNPC::StaticClass();
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	const float NpcStartX = bBuildingMode ? -170.f : -420.f;
	const float ExtinguisherX = bBuildingMode ? 0.f : -105.f;
	const float FireX = bBuildingMode ? 330.f : 440.f;
	const FVector NpcStart = StageOrigin + FVector(NpcStartX, 0.f, 92.f);
	DemoNpc = GetWorld()->SpawnActor<AYUFSEvacuationNPC>(NpcClass, NpcStart, FRotator::ZeroRotator, SpawnParameters);
	Extinguisher = GetWorld()->SpawnActor<AYUFSFireExtinguisher>(
		AYUFSFireExtinguisher::StaticClass(),
		StageOrigin + FVector(ExtinguisherX, 0.f, 0.f),
		FRotator::ZeroRotator,
		SpawnParameters);
	FireSource = GetWorld()->SpawnActor<AYUFSSuppressibleFireSource>(
		AYUFSSuppressibleFireSource::StaticClass(),
		StageOrigin + FVector(FireX, 0.f, 0.f),
		FRotator::ZeroRotator,
		SpawnParameters);

	if (!DemoNpc || !Extinguisher || !FireSource)
	{
		EnterPhase(EYUFSExtinguisherDemoPhase::Failed, TEXT("Could not spawn demo actors"));
		return;
	}

	DemoNpc->SetActorEnableCollision(false);
	DemoNpc->bUseExternalMotionDriver = false;
	DemoNpc->SetTimelinePlaybackMode(true);
	DemoNpc->SetAnimationShowcaseDebugSuppressed(true);
	CopyNpcAppearance(VisualTemplateNpc, DemoNpc);
	if (USkeletalMeshComponent* Mesh = DemoNpc->GetMesh())
	{
		if (!Mesh->GetSkeletalMeshAsset())
		{
			if (USkeletalMesh* FallbackMesh = LoadObject<USkeletalMesh>(
				nullptr,
				TEXT("/Game/NPCs/Crawling__1_.Crawling__1_")))
			{
				Mesh->SetSkeletalMeshAsset(FallbackMesh);
				Mesh->SetRelativeLocation(FVector(0.f, 0.f, -90.f));
				Mesh->SetRelativeRotation(FRotator(0.f, -90.f, 0.f));
			}
		}
		Mesh->SetVisibility(true, true);
		UE_LOG(
			LogTemp,
			Display,
			TEXT("[YUFSExtinguisherDemo] NPCVisual Class=%s Mesh=%s Template=%s"),
			*DemoNpc->GetClass()->GetPathName(),
			Mesh->GetSkeletalMeshAsset() ? *Mesh->GetSkeletalMeshAsset()->GetPathName() : TEXT("None"),
			VisualTemplateNpc ? *VisualTemplateNpc->GetPathName() : TEXT("None"));
	}

	ExtinguisherUsePoint = StageOrigin + FVector(bBuildingMode ? -65.f : -170.f, 0.f, 92.f);
	FireAttackPoint = StageOrigin + FVector(bBuildingMode ? 105.f : 125.f, 0.f, 92.f);

	DemoCamera = GetWorld()->SpawnActor<ACameraActor>(
		ACameraActor::StaticClass(),
		StageOrigin + (bBuildingMode ? FVector(80.f, -440.f, 215.f) : FVector(10.f, -720.f, 245.f)),
		FRotator::ZeroRotator,
		SpawnParameters);
	if (DemoCamera)
	{
		DemoCamera->GetCameraComponent()->SetFieldOfView(bBuildingMode ? 52.f : 55.f);
		const FVector LookAt = StageOrigin + FVector(bBuildingMode ? 90.f : 80.f, 0.f, 100.f);
		DemoCamera->SetActorRotation((LookAt - DemoCamera->GetActorLocation()).Rotation());
	}

	TeamIntegration = DemoNpc->GetTeamIntegrationComponent();
	if (!TeamIntegration)
	{
		EnterPhase(EYUFSExtinguisherDemoPhase::Failed, TEXT("NPC has no team integration component"));
		return;
	}
	TeamIntegration->OnNavigationDirectiveChanged.AddDynamic(this, &AYUFSExtinguisherDemoDirector::OnNavigationDirective);
	TeamIntegration->OnMotionDirectiveChanged.AddDynamic(this, &AYUFSExtinguisherDemoDirector::OnMotionDirective);
	TeamIntegration->OnInteractionDirectiveChanged.AddDynamic(this, &AYUFSExtinguisherDemoDirector::OnInteractionDirective);

	Opportunities.KnowledgeRevision = 1;
	Opportunities.bExtinguisherKnownAvailable = true;
	Opportunities.ExtinguisherStableId = TEXT("EXT-DEMO-01");
	Opportunities.ExtinguisherLocation = Extinguisher->GetActorLocation();
	Opportunities.bSuppressibleFireKnown = true;
	Opportunities.FireStableId = TEXT("FIRE-DEMO-01");
	Opportunities.FireLocation = FireSource->GetActorLocation();
	Opportunities.bSafeRetreatKnown = true;
	Opportunities.KnowledgeConfidence = 1.f;
	TeamIntegration->SubmitInteractionOpportunities(Opportunities);

	SuppressionDecision.Revision = 1;
	SuppressionDecision.Behavior = EYUFSHighLevelBehavior::AttemptSuppression;
	SuppressionDecision.LegacyAction = EYUFSAction::Idle;
	SuppressionDecision.DesiredTask = EYUFSActionTask::InitialExtinguish;
	SuppressionDecision.Reason = TEXT("DemoSafetyGatePassed");
	DemoCognition.PerceivedRisk = 0.45f;
	DemoCognition.Urgency = 0.55f;
	DemoCognition.SituationConfidence = 1.f;
	PublishSuppressionDecision();
	EnterPhase(EYUFSExtinguisherDemoPhase::MovingToExtinguisher, TEXT("Directive: acquire the known available extinguisher"));
}

void AYUFSExtinguisherDemoDirector::PublishSuppressionDecision()
{
	TeamIntegration->PublishDecision(
		DemoNpcId,
		SuppressionDecision,
		EYUFSIntent::Prepare,
		EYUFSBehaviorState::Normal,
		FVector::ZeroVector,
		true,
		DemoCognition);
}

void AYUFSExtinguisherDemoDirector::EnterPhase(
	EYUFSExtinguisherDemoPhase NewPhase,
	const FString& Message)
{
	if (Phase == NewPhase && StatusMessage == Message)
	{
		return;
	}
	Phase = NewPhase;
	PhaseElapsedSeconds = 0.f;
	StatusMessage = Message;
	ApplyPhaseAnimation();
	UE_LOG(LogTemp, Display, TEXT("[YUFSExtinguisherDemo] Phase=%s Message=%s"), *GetPhaseName(), *StatusMessage);
}

void AYUFSExtinguisherDemoDirector::ApplyPhaseAnimation()
{
	if (!DemoNpc)
	{
		return;
	}

	switch (Phase)
	{
	case EYUFSExtinguisherDemoPhase::MovingToExtinguisher:
	case EYUFSExtinguisherDemoPhase::MovingToFire:
		// HelpOther uses the project's canonical Walking sequence on the same
		// skeleton as every placed evacuation NPC.
		DemoNpc->SetActionAnimationPreview(EYUFSAction::HelpOther);
		break;
	case EYUFSExtinguisherDemoPhase::PickingUp:
	case EYUFSExtinguisherDemoPhase::Suppressing:
		// Existing two-hand forward pose; motion team can replace this semantic
		// with a dedicated montage later without changing interaction logic.
		DemoNpc->SetActionAnimationPreview(EYUFSAction::GatherBelongings);
		break;
	case EYUFSExtinguisherDemoPhase::Succeeded:
	case EYUFSExtinguisherDemoPhase::Failed:
	default:
		DemoNpc->SetActionAnimationPreview(EYUFSAction::Idle);
		break;
	}
}

void AYUFSExtinguisherDemoDirector::CopyNpcAppearance(
	const AYUFSEvacuationNPC* SourceNpc,
	AYUFSEvacuationNPC* TargetNpc) const
{
	if (!SourceNpc || !TargetNpc || !SourceNpc->GetMesh() || !TargetNpc->GetMesh())
	{
		return;
	}

	const USkeletalMeshComponent* SourceMesh = SourceNpc->GetMesh();
	USkeletalMeshComponent* TargetMesh = TargetNpc->GetMesh();
	TargetMesh->SetSkeletalMeshAsset(SourceMesh->GetSkeletalMeshAsset());
	TargetMesh->SetRelativeTransform(SourceMesh->GetRelativeTransform());
	TargetMesh->SetBoundsScale(SourceMesh->BoundsScale);
	TargetMesh->SetOverlayMaterial(SourceMesh->GetOverlayMaterial());
	for (int32 MaterialIndex = 0; MaterialIndex < SourceMesh->GetNumMaterials(); ++MaterialIndex)
	{
		TargetMesh->SetMaterial(MaterialIndex, SourceMesh->GetMaterial(MaterialIndex));
	}
}

void AYUFSExtinguisherDemoDirector::MoveNpcToward(
	const FVector& Destination,
	float DeltaTime,
	float SpeedCmPerSecond)
{
	const FVector Current = DemoNpc->GetActorLocation();
	const FVector Next = FMath::VInterpConstantTo(Current, Destination, DeltaTime, SpeedCmPerSecond);
	const FVector Direction = Destination - Current;
	if (!Direction.IsNearlyZero())
	{
		DemoNpc->SetActorRotation(FRotator(0.f, Direction.Rotation().Yaw, 0.f));
	}
	DemoNpc->SetActorLocation(Next, false, nullptr, ETeleportType::TeleportPhysics);
}

void AYUFSExtinguisherDemoDirector::UpdateHeldExtinguisherTransform()
{
	if (!DemoNpc || !Extinguisher)
	{
		return;
	}

	const USkeletalMeshComponent* Mesh = DemoNpc->GetMesh();
	const FName RightHandName = ResolveRightHandName(Mesh);
	const FVector HandLocation = Mesh && !RightHandName.IsNone()
		? Mesh->GetSocketLocation(RightHandName)
		: DemoNpc->GetActorLocation() + FVector(0.f, 0.f, 120.f);
	const FVector DesiredLocation = HandLocation
		- FVector::UpVector * 47.f
		- DemoNpc->GetActorRightVector() * 28.f
		+ DemoNpc->GetActorForwardVector() * 8.f;
	// The imported horn points along local +Y. Yaw -90 aligns it with the NPC's
	// local +X/forward direction while keeping the tank vertical.
	const FRotator DesiredRotation(0.f, DemoNpc->GetActorRotation().Yaw - 90.f, 0.f);
	Extinguisher->SetActorLocationAndRotation(
		DesiredLocation,
		DesiredRotation,
		false,
		nullptr,
		ETeleportType::TeleportPhysics);
}

void AYUFSExtinguisherDemoDirector::AcquireExtinguisher()
{
	EnterPhase(EYUFSExtinguisherDemoPhase::PickingUp, TEXT("Reservation accepted; binding extinguisher to resolved right hand"));
	if (!Extinguisher->TryReserve(DemoNpc))
	{
		EnterPhase(EYUFSExtinguisherDemoPhase::Failed, TEXT("Reservation rejected"));
		return;
	}

	USceneComponent* AttachParent = DemoNpc->GetMesh() ? Cast<USceneComponent>(DemoNpc->GetMesh()) : DemoNpc->GetRootComponent();
	const FName SocketName = ResolveRightHandName(DemoNpc->GetMesh());
	if (!Extinguisher->PickUp(DemoNpc, AttachParent, SocketName))
	{
		EnterPhase(EYUFSExtinguisherDemoPhase::Failed, TEXT("Pickup failed"));
		return;
	}
	// The demonstration director supplies a stable two-hand presentation pose
	// without depending on a project-specific hand socket orientation. Detach
	// after the real pickup state transition, then drive the prop in world space.
	// Production motion can replace this with socket attachment + hand IK.
	Extinguisher->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	UpdateHeldExtinguisherTransform();

	FYUFSTeamRequestFeedback Feedback;
	Feedback.RequestRevision = LastInteractionDirective.Revision;
	Feedback.Status = EYUFSTeamRequestStatus::Completed;
	Feedback.Reason = TEXT("ExtinguisherAcquired");
	Feedback.ResolvedLocation = DemoNpc->GetActorLocation();
	TeamIntegration->SubmitInteractionFeedback(Feedback);

	Opportunities.KnowledgeRevision++;
	Opportunities.bExtinguisherKnownAvailable = false;
	Opportunities.bHoldingExtinguisher = true;
	TeamIntegration->SubmitInteractionOpportunities(Opportunities);
	PublishSuppressionDecision();
	EnterPhase(EYUFSExtinguisherDemoPhase::MovingToFire, TEXT("Feedback complete; new directive: suppress initial fire"));
}

void AYUFSExtinguisherDemoDirector::BeginSuppression()
{
	if (!Extinguisher->StartSpraying(DemoNpc))
	{
		EnterPhase(EYUFSExtinguisherDemoPhase::Failed, TEXT("Spray rejected by ownership/state check"));
		return;
	}
	FYUFSTeamRequestFeedback Feedback;
	Feedback.RequestRevision = LastInteractionDirective.Revision;
	Feedback.Status = EYUFSTeamRequestStatus::InProgress;
	Feedback.Reason = TEXT("SuppressionInProgress");
	Feedback.ResolvedLocation = FireAttackPoint;
	TeamIntegration->SubmitInteractionFeedback(Feedback);
	SprayBeam->SetVisibility(true);
	EnterPhase(EYUFSExtinguisherDemoPhase::Suppressing, TEXT("PASS: pin, aim, squeeze, sweep"));
}

void AYUFSExtinguisherDemoDirector::FinishSuppression()
{
	Extinguisher->StopSpraying(DemoNpc);
	SprayBeam->SetVisibility(false);
	FYUFSTeamRequestFeedback Feedback;
	Feedback.RequestRevision = LastInteractionDirective.Revision;
	Feedback.Status = EYUFSTeamRequestStatus::Completed;
	Feedback.Reason = TEXT("FireExtinguished");
	Feedback.ResolvedLocation = FireSource->GetActorLocation();
	TeamIntegration->SubmitInteractionFeedback(Feedback);
	EnterPhase(EYUFSExtinguisherDemoPhase::Succeeded, TEXT("Interaction feedback: COMPLETED / fire extinguished"));
}

void AYUFSExtinguisherDemoDirector::UpdateCamera()
{
	if (bCameraAssigned || !DemoCamera)
	{
		return;
	}
	if (APlayerController* PlayerController = UGameplayStatics::GetPlayerController(this, 0))
	{
		PlayerController->SetViewTargetWithBlend(DemoCamera, 0.35f);
		bCameraAssigned = true;
	}
}

void AYUFSExtinguisherDemoDirector::UpdateSprayBeam()
{
	if (!SprayBeam->IsVisible())
	{
		return;
	}
	const FVector Start = Extinguisher->GetNozzleWorldLocation();
	const FVector SweepOffset = DemoNpc->GetActorRightVector()
		* (FMath::Sin(PhaseElapsedSeconds * 2.25f) * 32.f);
	const FVector End = FireSource->GetActorLocation() + FVector(0.f, 0.f, 55.f) + SweepOffset;
	const FVector Direction = End - Start;
	SprayBeam->SetWorldLocation((Start + End) * 0.5f);
	SprayBeam->SetWorldRotation(FRotationMatrix::MakeFromZ(Direction).Rotator());
	SprayBeam->SetWorldScale3D(FVector(0.018f, 0.018f, Direction.Size() / 100.f));
}

void AYUFSExtinguisherDemoDirector::UpdateScreenStatus() const
{
	if (!GEngine)
	{
		return;
	}
	GEngine->AddOnScreenDebugMessage(
		900100,
		0.f,
		FColor(90, 220, 255),
		TEXT("YUFS EXTINGUISHER INTERACTION DEMO"),
		true,
		FVector2D(1.45f));
	GEngine->AddOnScreenDebugMessage(
		900101,
		0.f,
		Phase == EYUFSExtinguisherDemoPhase::Succeeded ? FColor::Green : FColor::White,
		FString::Printf(TEXT("Phase: %s | %s"), *GetPhaseName(), *StatusMessage),
		true,
		FVector2D(1.15f));
	GEngine->AddOnScreenDebugMessage(
		900102,
		0.f,
		FColor::Yellow,
		FString::Printf(
			TEXT("Navigation: %s  |  Motion: %s  |  Interaction: %s"),
			*UEnum::GetDisplayValueAsText(LastNavigationDirective.Goal).ToString(),
			*UEnum::GetDisplayValueAsText(LastMotionDirective.Semantic).ToString(),
			*UEnum::GetDisplayValueAsText(LastInteractionDirective.Goal).ToString()),
		true,
		FVector2D(1.0f));
	GEngine->AddOnScreenDebugMessage(
		900103,
		0.f,
		FColor::Silver,
		FString::Printf(
			TEXT("Safety gates: trained YES | initial fire YES | safe retreat YES    Agent %.0f%% | Fire %.0f%%"),
			Extinguisher ? Extinguisher->GetRemainingAgentNormalized() * 100.f : 0.f,
			FireSource ? FireSource->GetIntensityNormalized() * 100.f : 0.f),
		true,
		FVector2D(1.0f));
}

FString AYUFSExtinguisherDemoDirector::GetPhaseName() const
{
	switch (Phase)
	{
	case EYUFSExtinguisherDemoPhase::Setup: return TEXT("SETUP");
	case EYUFSExtinguisherDemoPhase::MovingToExtinguisher: return TEXT("MOVE TO EXTINGUISHER");
	case EYUFSExtinguisherDemoPhase::PickingUp: return TEXT("PICK UP");
	case EYUFSExtinguisherDemoPhase::MovingToFire: return TEXT("MOVE TO FIRE");
	case EYUFSExtinguisherDemoPhase::Suppressing: return TEXT("SUPPRESSING");
	case EYUFSExtinguisherDemoPhase::Succeeded: return TEXT("SUCCESS");
	case EYUFSExtinguisherDemoPhase::Failed: return TEXT("FAILED");
	default: return TEXT("UNKNOWN");
	}
}

void AYUFSExtinguisherDemoDirector::OnNavigationDirective(const FYUFSNavigationDirective& Directive)
{
	LastNavigationDirective = Directive;
}

void AYUFSExtinguisherDemoDirector::OnMotionDirective(const FYUFSMotionDirective& Directive)
{
	LastMotionDirective = Directive;
}

void AYUFSExtinguisherDemoDirector::OnInteractionDirective(const FYUFSInteractionDirective& Directive)
{
	LastInteractionDirective = Directive;
}
