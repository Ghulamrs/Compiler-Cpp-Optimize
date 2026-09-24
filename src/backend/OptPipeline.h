#pragma once

// **The pipeline**: the one place the passes are named in order. See
// OptPipeline.cpp for the declaration, and docs/OPTIMIZER-ARCHITECTURE.md
// for how a pass is added to it.

#include "OptPass.h"

namespace opt {

std::unique_ptr<Pass> pipelineFor();

}
