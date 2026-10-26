#pragma once
#include "../../circuitbreaker/src/trace.h"
namespace p8::rollback_crew {
// Actor-local U and J action receipts. No teammate ledger or collector receipt
// is an input; each J member proves its actual own share and own conversion.
using CycleTrace=circuitbreaker::JTrace;
}
