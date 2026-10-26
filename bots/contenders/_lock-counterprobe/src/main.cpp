#define counterlock_probe_mode 0
#define counterlock_active_side 0
#define counterlock_prelock false
#define counterlock_epoch 70

#if counterlock_probe_mode == 0
#include "production_entry.h"
#else
#include "probe.h"
#endif
