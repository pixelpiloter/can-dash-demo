// qt_binder.cpp
// 层: platform — 快照映射 + 上屏绑定（派生逻辑已下沉 logic.yaml）

#include "platform/qt/qt_binder.h"
#include "platform/latency_probe.h"

#include <algorithm>
#include <cmath>

namespace platform {

QtBinder::QtBinder(QObject* parent) : QObject(parent) {}

void QtBinder::installDataSource(std::unique_ptr<IDataSource> source) {
    // stop 必须先于 unique_ptr 替换：旧 source 可能还持有捕获 this 的 callback。
    stopDataSource();
    m_source = std::move(source);
    if (!m_source) return;

    // SnapshotPump 在独立线程产生快照；QObject 属性/信号必须在 GUI 线程处理，
    // 因此把回调经 QueuedConnection 投递到本对象线程（Qt 主线程）再处理。
    m_source->setUpdateCallback([this](const UiSnapshot& s) {
        QMetaObject::invokeMethod(
            this, [this, snap = s]() { onDataUpdated(snap); },
            Qt::QueuedConnection);
    });
    m_source->setHealthCallback([this](HealthStatus h) {
        QMetaObject::invokeMethod(
            this, [this, h]() { onHealthChanged(h); }, Qt::QueuedConnection);
    });
    m_sourceCallbacksInstalled = true;
    m_source->start();
}

void QtBinder::stopDataSource() {
    if (!m_source || !m_sourceCallbacksInstalled) return;
    m_source->stop();
    // stop() 撤销异步生产；清空回调同时切断 source 对 backend 的捕获，
    // 使重复 stop、source 替换和 backend 析构都不留下悬空调用入口。
    m_source->setUpdateCallback({});
    m_source->setHealthCallback({});
    m_sourceCallbacksInstalled = false;
}

QVariantMap QtBinder::signalsToMap(const std::unordered_map<std::string, double>& s) {
    QVariantMap m;
    for (const auto& [k, v] : s)
        m[QString::fromUtf8(k.c_str())] = v;
    return m;
}

QVariantMap QtBinder::displayValuesToMap(
    const std::unordered_map<std::string, clk::LogicEngine::DisplayEntry>& d) {
    QVariantMap m;
    for (const auto& [k, e] : d) {
        QVariantMap entry;
        entry["type"] = QString::fromUtf8(e.type.c_str());
        if (e.type == "string")
            entry["value"] = QString::fromUtf8(e.str.c_str());
        else
            entry["value"] = e.num;
        m[QString::fromUtf8(k.c_str())] = entry;
    }
    return m;
}

QVariantMap QtBinder::buildMergedDisplayData() const {
    QVariantMap merged = m_signalData;
    for (auto it = m_displayValues.constBegin(); it != m_displayValues.constEnd(); ++it) {
        const QVariantMap entry = it.value().toMap();
        const QString type = entry.value("type").toString();
        if (type == "string")
            merged[it.key()] = entry.value("value");
        else
            merged[it.key()] = entry.value("value").toDouble();
    }
    return merged;
}

QVariantMap QtBinder::buildLightStateMap(
    const std::unordered_map<std::string, clk::LogicEngine::LightState>& ls) const {
    QVariantMap m;
    for (const auto& [id, st] : ls) {
        QVariantMap v;
        v["on"] = st.on;
        v["flash"] = st.flash;
        m[QString::fromUtf8(id.c_str())] = v;
    }
    return m;
}

QVariantMap QtBinder::buildIndicatorStates(
    const std::unordered_map<std::string, clk::LogicEngine::LightState>& ls) const {
    QVariantMap m = buildLightStateMap(ls);
    for (auto it = m_qmlIndicatorOverrides.constBegin(); it != m_qmlIndicatorOverrides.constEnd(); ++it)
        m[it.key()] = it.value();
    return m;
}

QVariantList QtBinder::buildAlarmList(const UiSnapshot& s) const {
    QVariantList list;
    for (const auto& [id, active] : s.warn_state) {
        if (!active) continue;
        std::string priority;
        QVariantMap item;
        item["name"] = QString::fromUtf8(id.c_str());
        item["text_zh"] = QString::fromUtf8(id.c_str());
        item["text_en"] = QString::fromUtf8(id.c_str());
        item["priority"] = 128;
        item["color"] = QStringLiteral("#FF4400");
        item["font_size"] = 32;
        item["dedup_count"] = 1;
        if (m_catalog) {
            if (const WarnMeta* meta = m_catalog->warnMeta(id)) {
                if (!meta->text_zh.empty()) item["text_zh"] = QString::fromUtf8(meta->text_zh.c_str());
                if (!meta->text_en.empty()) item["text_en"] = QString::fromUtf8(meta->text_en.c_str());
                if (!meta->color.empty())   item["color"]   = QString::fromUtf8(meta->color.c_str());
                priority = meta->priority;
                if (!priority.empty())
                    item["priority"] = static_cast<int>(priority[0]);
            }
        }
        // priority A..D → severity(0=Info/1=Warning/2=Critical) + rank；
        // 缺失或非法值安全回退 Warning/rank1。
        int severity = 1;
        int rank = 1;
        if (priority == "A") { severity = 2; rank = 0; }
        else if (priority == "B") { severity = 1; rank = 1; }
        else if (priority == "C") { severity = 0; rank = 2; }
        else if (priority == "D") { severity = 0; rank = 3; }
        item["severity"] = severity;
        item["priority_rank"] = rank;
        list.append(item);
    }
    std::stable_sort(list.begin(), list.end(), [](const QVariant& lhs, const QVariant& rhs) {
        const QVariantMap left = lhs.toMap();
        const QVariantMap right = rhs.toMap();
        const int leftRank = left.value(QStringLiteral("priority_rank"), 1).toInt();
        const int rightRank = right.value(QStringLiteral("priority_rank"), 1).toInt();
        if (leftRank != rightRank) return leftRank < rightRank;
        return left.value(QStringLiteral("name")).toString()
            < right.value(QStringLiteral("name")).toString();
    });
    return list;
}

void QtBinder::onDataUpdated(const UiSnapshot& s) {
    bool healthDirty = false;
    const bool payloadGenerationChanged = s.meta.generation != m_lastDataGeneration;
    const bool staleSignalsChanged = s.stale_signals != m_staleSignals;
    if (staleSignalsChanged) m_staleSignals = s.stale_signals;

    // 仅在聚合 UI generation 变化时重建昂贵的 payload map 并发出细粒度信号。
    if (payloadGenerationChanged) {
        m_lastDataGeneration = s.meta.generation;

        const QVariantMap newSignals = signalsToMap(s.can_signals);
        if (newSignals != m_signalData) {
            m_signalData = newSignals;
            emit signalDataChanged();
        }

        const QVariantMap newDisp = displayValuesToMap(s.display_values);
        if (newDisp != m_displayValues) {
            m_displayValues = newDisp;
            emit displayValuesChanged();
        }

        const QVariantMap merged = buildMergedDisplayData();
        if (merged != m_displayData) {
            m_displayData = merged;
            LatencyProbe::instance().onQmlPush(monoUs(), s.meta.frame_seq);
            LatencyProbe::instance().maybeLogP95(monoUs());
            emit displayDataChanged();
        }

        const QVariantMap newLight = buildLightStateMap(s.light_state);
        if (newLight != m_lightState) {
            m_lightState = newLight;
            emit lightStateChanged();
        }

        const QVariantMap newInd = buildIndicatorStates(s.light_state);
        if (newInd != m_indicatorStates) {
            m_indicatorStates = newInd;
            emit indicatorStatesChanged();
        }

        QVariantMap newWarn;
        for (const auto& [id, on] : s.warn_state)
            newWarn[QString::fromUtf8(id.c_str())] = on;
        if (newWarn != m_warnState) {
            m_warnState = newWarn;
            emit warnStateChanged();
        }

        const QVariantList alarmList = buildAlarmList(s);
        const bool alarmActive = !alarmList.isEmpty();
        const bool hasCritical = std::any_of(
            alarmList.cbegin(), alarmList.cend(), [](const QVariant& alarm) {
                return alarm.toMap().value(QStringLiteral("severity")).toInt() == 2;
            });
        QString alarmMsg;
        if (alarmActive)
            alarmMsg = alarmList.first().toMap().value("text_zh").toString();
        bool alarmDirty = false;
        if (alarmActive != m_alarmActive) { m_alarmActive = alarmActive; alarmDirty = true; }
        if (hasCritical != m_hasCritical) { m_hasCritical = hasCritical; alarmDirty = true; }
        if (alarmMsg != m_alarmMessageZh) { m_alarmMessageZh = alarmMsg; alarmDirty = true; }
        if (alarmList != m_alarmList) { m_alarmList = alarmList; alarmDirty = true; }
        if (alarmDirty) emit alarmActiveChanged();

        // 行驶状态（canonical 车速 > 1 km/h）
        const auto speed_it = s.can_signals.find("vehicle_speed");
        const bool moving = speed_it != s.can_signals.end() && speed_it->second > 1.0;
        if (moving != m_isMoving) {
            m_isMoving = moving;
            emit movingChanged();
        }

        // timestamp_ms 来自最近 CAN 接收而非本次 UI tick；dataFps 仍受 payload
        // generation 门控，近似内容状态发布频率。
        m_prevTickMs = m_lastTickMs;
        m_lastTickMs = s.meta.timestamp_ms;
        if (m_prevTickMs > 0 && m_lastTickMs > m_prevTickMs) {
            const double dt = static_cast<double>(m_lastTickMs - m_prevTickMs);
            const double fps = dt > 0 ? 1000.0 / dt : 0.0;
            if (std::abs(fps - m_dataFps) > 0.1) { m_dataFps = fps; healthDirty = true; }
        }
    }

    // stale 集合可在 CAN payload 不变时由 Logic tick 单独变化。只在 payload/stale
    // 版本变化时重建 validity，避免每个 UI tick 复制信号图。
    if (payloadGenerationChanged || staleSignalsChanged) {
        QVariantMap validity;
        for (auto it = m_signalData.constBegin(); it != m_signalData.constEnd(); ++it) {
            const std::string name = it.key().toStdString();
            validity[it.key()] = m_staleSignals.find(name) == m_staleSignals.end();
        }
        if (validity != m_fieldValidity) {
            m_fieldValidity = validity;
            healthDirty = true;
        }
    }

    // AGE/SEQ 仅是 QML 诊断采样值，以 sample_time_ms 做约 10Hz 发布；这不改变
    // SnapshotPump 的逐 tick freshness / HealthStatus 判定。首次（包括 0ms）立即
    // 发布；时钟回退时重新建立基线，避免无符号差值下溢或长期停止更新。
    constexpr uint64_t kMetaPublishCadenceMs = 100;
    const uint64_t sampleTimeMs = s.meta.sample_time_ms;
    const bool firstMetaPublish = !m_hasPublishedMeta;
    const bool sampleTimeRolledBack =
        m_hasPublishedMeta && sampleTimeMs < m_lastMetaSampleTimeMs;
    const bool metaCadenceElapsed =
        m_hasPublishedMeta && sampleTimeMs >= m_lastMetaSampleTimeMs
        && sampleTimeMs - m_lastMetaSampleTimeMs >= kMetaPublishCadenceMs;
    if (firstMetaPublish || sampleTimeRolledBack || metaCadenceElapsed) {
        const qulonglong age =
            static_cast<qulonglong>(std::max<int64_t>(s.meta.data_age_ms, 0));
        const qulonglong seq = s.meta.frame_seq;
        if (firstMetaPublish || age != m_dataAgeMs || seq != m_frameSeq)
            healthDirty = true;
        m_dataAgeMs = age;
        m_frameSeq = seq;
        m_lastMetaSampleTimeMs = sampleTimeMs;
        m_hasPublishedMeta = true;
    }

    // 掉帧累计变化是诊断异常，绕过 AGE/SEQ cadence 立即通知。
    const qulonglong drops = s.meta.dropped_frames;
    if (drops != m_droppedFrames) { m_droppedFrames = drops; healthDirty = true; }
    if (healthDirty) emit dataHealthChanged();
}

void QtBinder::onHealthChanged(HealthStatus h) {
    const bool online = (h == HealthStatus::Ok);
    const QString status = QString::fromUtf8(healthStatusStr(h));
    if (online != m_processorOnline || status != m_processorStatus) {
        m_processorOnline = online;
        m_processorStatus = status;
        emit healthChanged();
    }
}

QVariant QtBinder::signal(const QString& key) const { return m_signalData.value(key); }

QVariant QtBinder::displayValue(const QString& key) const {
    return m_displayValues.value(key).toMap().value("value");
}

bool QtBinder::lightOn(const QString& id) const {
    return m_lightState.value(id).toMap().value("on").toBool();
}

bool QtBinder::lightFlash(const QString& id) const {
    return m_lightState.value(id).toMap().value("flash").toBool();
}

bool QtBinder::warnOn(const QString& id) const { return m_warnState.value(id).toBool(); }

bool QtBinder::indicatorOn(const QString& key) const {
    return m_indicatorStates.value(key).toMap().value("on").toBool();
}

void QtBinder::setIndicator(const QString& id, bool on, bool flash, float hz) {
    QVariantMap st;
    st["on"] = on;
    st["flash"] = flash;
    st["hz"] = hz;
    m_qmlIndicatorOverrides[id] = st;
    m_indicatorStates[id] = st;
    emit indicatorStatesChanged();
}

}  // namespace platform
