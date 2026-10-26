#include "runtime.h"
#define PLAN4_RUNTIME_TYPE plan4::runtime::Bulkheads
#define PLAN4_MODEL_ENABLED 1
#define PLAN4_ACTOR_EVIDENCE 1
#define PLAN4_SPATIAL_EVIDENCE 1
// Ordinary play has no certified exogenous fixture policy or frozen cohort.
// The default Runtime reports this unsupported and forecasts exact M0.
#define PLAN4_DRAFT_PROFILE plan4::drafting::Profile::bulkheads
#include "../../common/model-main.h"
