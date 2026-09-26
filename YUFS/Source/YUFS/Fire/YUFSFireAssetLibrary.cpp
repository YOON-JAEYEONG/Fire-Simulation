#include "Fire/YUFSFireAssetLibrary.h"

#include "Materials/MaterialInstanceConstant.h"
#include "StaticParameterSet.h"

bool UYUFSFireAssetLibrary::SetStaticComponentMaskParameter(UMaterialInstanceConstant* Instance, FName ParameterName,
	bool bR, bool bG, bool bB, bool bA)
{
#if WITH_EDITOR
	if (!IsValid(Instance) || ParameterName.IsNone()) return false;
	FStaticParameterSet Parameters;
	Instance->GetStaticParameterValues(Parameters);
	bool bFound = false;
	for (FStaticComponentMaskParameter& Mask : Parameters.EditorOnly.StaticComponentMaskParameters)
	{
		if (Mask.ParameterInfo.Name != ParameterName) continue;
		Mask.R = bR; Mask.G = bG; Mask.B = bB; Mask.A = bA;
		Mask.bOverride = true;
		bFound = true;
	}
	if (!bFound) return false;
	Instance->UpdateStaticPermutation(Parameters);
	Instance->MarkPackageDirty();
	return true;
#else
	return false;
#endif
}
