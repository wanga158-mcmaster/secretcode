#include "sdk-collector.h"
#define PLAN4_RUNTIME_TYPE plan4::runtime::KernelLive
#define PLAN4_MODEL_ENABLED 1
#define PLAN4_ACTOR_EVIDENCE 1
#define PLAN4_PUBLIC_COMPOSITION 1
// Own exact-kit observations do not supply a frozen production fit table.
// Missing all-started/economic receipts keeps empirical support unavailable.
#define PLAN4_DRAFT_PROFILE plan4::drafting::Profile::kernel
#include "../../common/model-main.h"
