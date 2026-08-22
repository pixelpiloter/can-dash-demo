// idata_binder.h — UiSnapshot → UI（Kanzi）
#pragma once

#include "ui_snapshot.h"

namespace platform {

class IDataBinder {
public:
    virtual ~IDataBinder() = default;
    virtual void onDataUpdated(const UiSnapshot& snapshot) = 0;
    virtual void onHealthChanged(HealthStatus new_health) = 0;
};

}  // namespace platform
