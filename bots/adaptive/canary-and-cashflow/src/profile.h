#pragma once
#include "../../common/src/profile.h"
namespace p8::canary {
using namespace p8::common;
inline bool canary_actor(const p6::Actor& actor,int picks) { return common::leader_actor(actor,picks); }
}
