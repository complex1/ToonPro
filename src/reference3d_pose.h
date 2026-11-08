#pragma once
#include "reference3d.h"

namespace toon {
// Rotation-only analytic two-bone IK. Node indices must form upper -> middle ->
// end direct parent links. Targets and pole are WORLD coordinates, including
// placement. Imported rest axes are preserved through world/local quaternion
// conversion. Unreachable targets clamp to the chain's reachable interval.
// The instance changes only after complete validation and successful evaluation.
// Positive uniform ancestor/placement scales are required; unsupported scale
// chains, zero-length bones, locked instances and malformed inputs fail atomically.
bool solveReferenceLimbIK(ReferenceInstance &instance, int upperNode, int middleNode,
                          int endNode, QVector3D worldTarget, QVector3D worldPole,
                          QString *error = nullptr);
}
