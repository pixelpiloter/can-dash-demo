// store_binder.h — IDataBinder → DataStore
#pragma once

#include "idata_binder.h"
#include "data_store.h"

#include <cstdint>

namespace platform {

class StoreBinder : public IDataBinder {
public:
    explicit StoreBinder(cluster::DataStore* store);

    void onDataUpdated(const UiSnapshot& snapshot) override;
    void onHealthChanged(HealthStatus new_health) override;

private:
    cluster::DataStore* m_store = nullptr;
    uint64_t m_lastGeneration = UINT64_MAX;
};

}  // namespace platform
