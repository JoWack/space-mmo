#include "SpaceMMOCoordinates.h"

// The coordinate types are header-only by design: every operation is small arithmetic that
// benefits from inlining, and they sit on the hot path for every replicated position in the game.
//
// This translation unit exists so the module has something to compile for them and so the
// generated reflection code has a home.
