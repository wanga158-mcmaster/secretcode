#include "sdk-collector.h"
#define PLAN4_RUNTIME_TYPE plan4::runtime::SdkMetronome
#define PLAN4_MODEL_ENABLED 1
#define PLAN4_SPATIAL_EVIDENCE 1
// No native cost envelope/reference limitation is fabricated. Until a retained
// frozen profile is bound, both arms explicitly report unsupported and use M0.
#define PLAN4_DRAFT_PROFILE plan4::drafting::Profile::metronome
#include "../../common/model-main.h"
