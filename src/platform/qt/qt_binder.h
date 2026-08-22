// qt_binder.h
// 层: platform (Qt 上屏出口，对齐 KanziNativeBinder 的薄绑定思想)
// 职责: UiSnapshot → Q_PROPERTY；只绑定 signal/display/warn/light/健康，
//       并直接把 SnapshotPump 快照（QueuedConnection）投递到 GUI 线程。
// 派生逻辑已下沉 logic.yaml 规则引擎，Qt 与 Kanzi 同源。
#pragma once

#include "platform/config_catalog.h"
#include "platform/idata_binder.h"
#include "platform/idata_source.h"

#include <QObject>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <QString>

#include <limits>
#include <memory>
#include <unordered_set>

namespace platform {

class QtBinder : public QObject, public IDataBinder {
    Q_OBJECT

    // ── logic.yaml 驱动（上屏键与 Kanzi #DataSource 同名）──
    Q_PROPERTY(QVariantMap signalData READ signalData NOTIFY signalDataChanged)
    Q_PROPERTY(QVariantMap displayValues READ displayValues NOTIFY displayValuesChanged)
    Q_PROPERTY(QVariantMap displayData READ displayData NOTIFY displayDataChanged)
    Q_PROPERTY(QVariantMap lightState READ lightState NOTIFY lightStateChanged)
    Q_PROPERTY(QVariantMap indicatorStates READ indicatorStates NOTIFY indicatorStatesChanged)
    Q_PROPERTY(QVariantMap warnState READ warnState NOTIFY warnStateChanged)
    Q_PROPERTY(bool alarmActive READ alarmActive NOTIFY alarmActiveChanged)
    Q_PROPERTY(QString alarmMessageZh READ alarmMessageZh NOTIFY alarmActiveChanged)
    Q_PROPERTY(QVariantList alarmList READ alarmList NOTIFY alarmActiveChanged)
    Q_PROPERTY(bool isMoving READ isMoving NOTIFY movingChanged)
    Q_PROPERTY(bool processorOnline READ processorOnline NOTIFY healthChanged)
    Q_PROPERTY(QString processorStatus READ processorStatus NOTIFY healthChanged)
    Q_PROPERTY(qulonglong dataAgeMs READ dataAgeMs NOTIFY dataHealthChanged)
    Q_PROPERTY(qulonglong frameSeq READ frameSeq NOTIFY dataHealthChanged)
    Q_PROPERTY(double dataFps READ dataFps NOTIFY dataHealthChanged)
    Q_PROPERTY(qulonglong droppedFrames READ droppedFrames NOTIFY dataHealthChanged)
    Q_PROPERTY(QVariantMap fieldValidity READ fieldValidity NOTIFY dataHealthChanged)
    // warningActiveList 是 alarmList 的全量 active 兼容别名；hasCritical 从列表派生。
    Q_PROPERTY(QVariantList warningActiveList READ warningActiveList NOTIFY alarmActiveChanged)
    Q_PROPERTY(int warningCount READ warningCount NOTIFY alarmActiveChanged)
    Q_PROPERTY(bool hasCritical READ hasCritical NOTIFY alarmActiveChanged)

public:
    explicit QtBinder(QObject* parent = nullptr);

    void setCatalog(const ConfigCatalog* catalog) { m_catalog = catalog; }

    void onDataUpdated(const UiSnapshot& snapshot) override;
    void onHealthChanged(HealthStatus new_health) override;

    // 数据源安装（SnapshotPump 在独立线程采样；回调经 QueuedConnection 投递到 GUI 线程）
    void installDataSource(std::unique_ptr<IDataSource> source);
    void stopDataSource();

    QVariantMap signalData() const { return m_signalData; }
    QVariantMap displayValues() const { return m_displayValues; }
    QVariantMap displayData() const { return m_displayData; }
    QVariantMap lightState() const { return m_lightState; }
    QVariantMap indicatorStates() const { return m_indicatorStates; }
    QVariantMap warnState() const { return m_warnState; }
    bool alarmActive() const { return m_alarmActive; }
    QString alarmMessageZh() const { return m_alarmMessageZh; }
    QVariantList alarmList() const { return m_alarmList; }
    bool isMoving() const { return m_isMoving; }
    bool processorOnline() const { return m_processorOnline; }
    QString processorStatus() const { return m_processorStatus; }
    qulonglong dataAgeMs() const { return m_dataAgeMs; }
    qulonglong frameSeq() const { return m_frameSeq; }
    double dataFps() const { return m_dataFps; }
    qulonglong droppedFrames() const { return m_droppedFrames; }
    QVariantMap fieldValidity() const { return m_fieldValidity; }
    QVariantList warningActiveList() const { return m_alarmList; }
    int warningCount() const { return m_alarmList.size(); }
    bool hasCritical() const { return m_hasCritical; }

    Q_INVOKABLE QVariant signal(const QString& key) const;
    Q_INVOKABLE QVariant displayValue(const QString& key) const;
    Q_INVOKABLE bool lightOn(const QString& id) const;
    Q_INVOKABLE bool lightFlash(const QString& id) const;
    Q_INVOKABLE bool warnOn(const QString& id) const;
    Q_INVOKABLE bool indicatorOn(const QString& key) const;
    Q_INVOKABLE void setIndicator(const QString& id, bool on, bool flash = false, float hz = 1.0f);

signals:
    void signalDataChanged();
    void displayValuesChanged();
    void displayDataChanged();
    void lightStateChanged();
    void indicatorStatesChanged();
    void warnStateChanged();
    void alarmActiveChanged();
    void movingChanged();
    void healthChanged();
    void dataHealthChanged();

private:
    static QVariantMap signalsToMap(const std::unordered_map<std::string, double>& s);
    static QVariantMap displayValuesToMap(
        const std::unordered_map<std::string, clk::LogicEngine::DisplayEntry>& d);
    QVariantMap buildMergedDisplayData() const;
    QVariantMap buildLightStateMap(
        const std::unordered_map<std::string, clk::LogicEngine::LightState>& ls) const;
    QVariantMap buildIndicatorStates(
        const std::unordered_map<std::string, clk::LogicEngine::LightState>& ls) const;
    QVariantList buildAlarmList(const UiSnapshot& s) const;

    const ConfigCatalog* m_catalog = nullptr;
    std::unique_ptr<IDataSource> m_source;
    bool m_sourceCallbacksInstalled = false;

    QVariantMap m_signalData;
    QVariantMap m_displayValues;
    QVariantMap m_displayData;
    QVariantMap m_lightState;
    QVariantMap m_indicatorStates;
    QVariantMap m_warnState;
    QVariantMap m_qmlIndicatorOverrides;

    bool m_alarmActive = false;
    bool m_hasCritical = false;
    QString m_alarmMessageZh;
    QVariantList m_alarmList;

    bool m_processorOnline = false;
    QString m_processorStatus = QStringLiteral("disconnected");
    qulonglong m_dataAgeMs = 0;
    qulonglong m_frameSeq = 0;
    double m_dataFps = 0.0;
    qulonglong m_droppedFrames = 0;
    QVariantMap m_fieldValidity;
    std::unordered_set<std::string> m_staleSignals;
    bool m_isMoving = false;

    uint64_t m_lastTickMs = 0;
    uint64_t m_prevTickMs = 0;
    uint64_t m_lastDataGeneration = std::numeric_limits<uint64_t>::max();
    // QML AGE/SEQ 是诊断采样值；独立状态避免其它 dataHealth 字段变化带出每 tick meta。
    uint64_t m_lastMetaSampleTimeMs = 0;
    bool m_hasPublishedMeta = false;
};

}  // namespace platform
