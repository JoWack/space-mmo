#pragma once

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Helpers shared by the panel tests.
 *
 * <strong>Shared because they must be.</strong> Each panel test file had its own copy inside an
 * anonymous namespace, which is fine across translation units and breaks the moment a unity build
 * puts two of them in the same one — anonymous namespaces in a single translation unit are the same
 * namespace, so the second definition is a redefinition. Adding one unrelated file to this module
 * re-grouped the unity blob and turned three latent duplicates into build errors at once.
 *
 * `inline` rather than `static`, so every file that includes this shares one definition rather than
 * carrying its own.
 */
namespace SpaceMMOPanelTests
{
	/** True if any line contains the fragment. Layout is free to change; the facts are not. */
	inline bool AnyLineContains(const TArray<FString>& Lines, const FString& Fragment)
	{
		return Lines.ContainsByPredicate(
			[&Fragment](const FString& Line) { return Line.Contains(Fragment); });
	}

	/** The index of the first line containing the fragment, or INDEX_NONE. */
	inline int32 IndexOfLineContaining(const TArray<FString>& Lines, const FString& Fragment)
	{
		return Lines.IndexOfByPredicate(
			[&Fragment](const FString& Line) { return Line.Contains(Fragment); });
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS
