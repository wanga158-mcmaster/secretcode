#include "catalog.h"
#include "catalog_data.h"

namespace zero {

const Catalog& catalog()
{
    static const Catalog c{ data::kSettings,
                            data::kChampions, static_cast<int>(sizeof(data::kChampions) / sizeof(data::kChampions[0])),
                            data::kCamps, static_cast<int>(sizeof(data::kCamps) / sizeof(data::kCamps[0])),
                            data::kItems, static_cast<int>(sizeof(data::kItems) / sizeof(data::kItems[0])),
                            data::kPassives, static_cast<int>(sizeof(data::kPassives) / sizeof(data::kPassives[0])) };
    return c;
}

}  // namespace zero
