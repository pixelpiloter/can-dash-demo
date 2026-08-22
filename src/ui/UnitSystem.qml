// UnitSystem.qml — 展示层唯一单位换算入口。
// CAN、logic.yaml 始终保留公制 canonical 值；调用方只在
// 绑定到用户可见文本、表盘或曲线时使用本组件转换。
import QtQml 2.15

QtObject {
    id: root

    // 0 = metric，1 = imperial；未知值按 metric 处理。
    property int units: 0

    readonly property bool imperial: units === 1
    readonly property real kilometersToMiles: 0.621371192237334
    readonly property real speedScale: imperial ? kilometersToMiles : 1.0
    readonly property real distanceScale: imperial ? kilometersToMiles : 1.0

    readonly property string speedLabel: imperial ? "mph" : "km/h"
    readonly property string distanceLabel: imperial ? "mi" : "km"
    readonly property string temperatureLabel: imperial ? "°F" : "°C"

    function kmhToMph(kmh) {
        return Number(kmh) * root.kilometersToMiles
    }

    function kmToMi(km) {
        return Number(km) * root.kilometersToMiles
    }

    function celsiusToFahrenheit(celsius) {
        return Number(celsius) * 9.0 / 5.0 + 32.0
    }

    function speedFromKmh(kmh) {
        return root.imperial ? root.kmhToMph(kmh) : Number(kmh)
    }

    function distanceFromKm(km) {
        return root.imperial ? root.kmToMi(km) : Number(km)
    }

    function temperatureFromCelsius(celsius) {
        return root.imperial ? root.celsiusToFahrenheit(celsius) : Number(celsius)
    }

    // 固定小数位并消除舍入后出现的 “-0”，保证单位切换时文本稳定。
    function formatNumber(value, decimals) {
        var number = Number(value)
        if (!isFinite(number))
            return "--"
        var digits = Math.max(0, Math.min(6, Math.floor(Number(decimals))))
        var threshold = 0.5 / Math.pow(10, digits)
        if (Math.abs(number) < threshold)
            number = 0
        return number.toFixed(digits)
    }

    function formatSpeed(kmh, decimals) {
        return root.formatNumber(root.speedFromKmh(kmh), decimals)
    }

    function formatDistance(km, decimals) {
        return root.formatNumber(root.distanceFromKm(km), decimals)
    }

    function formatTemperature(celsius, decimals) {
        return root.formatNumber(root.temperatureFromCelsius(celsius), decimals)
    }
}
