#pragma once

#include "ui_snapshot.h"
#include <functional>

namespace platform {

class IDataSource {
public:
    using UpdateCallback = std::function<void(const UiSnapshot&)>;
    using HealthCallback = std::function<void(HealthStatus)>;

    virtual ~IDataSource() = default;
    virtual bool start() = 0;
    virtual void stop() = 0;
    virtual bool isRunning() const = 0;
    virtual UiSnapshot snapshot() const = 0;
    virtual HealthStatus health() const = 0;
    virtual void setUpdateCallback(UpdateCallback cb) = 0;
    virtual void setHealthCallback(HealthCallback cb) = 0;
};

}  // namespace platform
