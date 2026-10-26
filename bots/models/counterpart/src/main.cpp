#include "runtime.h"
// No public profile provider is installed: both arms deliberately use M0
// until the module/law/order/observability/first-step certificates are bound.
#define PLAN4_RUNTIME_TYPE plan4::runtime::Counterpart
#define PLAN4_MODEL_ENABLED 1
#define PLAN4_ACTOR_EVIDENCE 1
#define PLAN4_SPATIAL_EVIDENCE 1
#define PLAN4_DRAFT_PROFILE plan4::drafting::Profile::counterpart
#include "../../common/model-main.h"
