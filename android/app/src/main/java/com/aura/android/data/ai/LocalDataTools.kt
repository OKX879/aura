package com.aura.android.data.ai

import com.aura.android.data.LocalMode
import com.aura.android.data.api.ApiClient
import com.aura.android.data.model.VitalsPoint
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.booleanOrNull
import kotlinx.serialization.json.buildJsonObject
import kotlinx.serialization.json.intOrNull
import kotlinx.serialization.json.jsonPrimitive
import kotlinx.serialization.json.put
import kotlinx.serialization.json.putJsonObject
import java.time.Instant
import java.time.LocalDate
import java.time.LocalDateTime
import java.time.OffsetDateTime
import java.time.ZoneId
import java.time.format.DateTimeFormatter

// 供模型主动调用的本地数据工具
object LocalDataTools {

    val definitions: List<JsonObject> = listOf(
        function(
            name = "query_vitals",
            description = "按时间范围查询当前用户的体征记录（心率/呼吸/专注/压力/疲劳），返回汇总与逐点明细。用于历史对比、定位异常时段、更长时段趋势。",
            properties = {
                number("hours", "回看小时数，1-168，默认 6")
                number("limit", "最多返回多少条，1-240，默认 60")
            }
        ),
        function(
            name = "query_alerts",
            description = "查询告警记录，可按时间范围和是否未处理过滤。用于判断近期是否发生设备离线、雷达中断、心率异常等事件。",
            properties = {
                number("hours", "回看小时数，1-168，默认 24")
                bool("unresolved_only", "是否只看未处理告警，默认 false")
            }
        ),
        function(
            name = "query_devices",
            description = "查询设备列表与在线状态，用于判断数据是否来自正常工作的雷达。",
            properties = {}
        ),
        function(
            name = "query_live",
            description = "查询雷达实时链路和当帧算法细节（帧数、错误、丢帧、置信度、相干性、FFT/ACF/PPI 心率等），用于判断数据质量与算法可信度。",
            properties = {}
        )
    )

    suspend fun execute(name: String, args: JsonObject): String = runCatching {
        when (name) {
            "query_vitals" -> queryVitals(args)
            "query_alerts" -> queryAlerts(args)
            "query_devices" -> queryDevices()
            "query_live" -> queryLive()
            else -> "未知工具：$name"
        }
    }.getOrElse { "查询失败：${it.message ?: "未知错误"}" }

    private suspend fun queryVitals(args: JsonObject): String {
        if (LocalMode.ENABLED) return "当前是本地演示模式，没有可查询的历史记录，请使用快照数据。"
        val hours = args.intOr("hours", 6).coerceIn(1, 168)
        val limit = args.intOr("limit", 60).coerceIn(1, 240)
        val points = ApiClient.auraApi.getRecords(hours, limit).data.orEmpty()
        if (points.isEmpty()) return "近 $hours 小时没有体征记录。"
        return buildString {
            appendLine("近 $hours 小时共 ${points.size} 条记录（${localTime(points.first().time)}–${localTime(points.last().time)}）。")
            appendLine(
                "心率 ${range(points.mapNotNull { it.heartRate?.toFloat() })}｜" +
                    "呼吸 ${range(points.mapNotNull { it.respirationRate?.toFloat() })}"
            )
            appendLine(
                "专注 ${range(points.map { it.focusScore.toFloat() })}｜" +
                    "压力 ${range(points.mapNotNull { it.stressLevel?.toFloat() })}｜" +
                    "疲劳 ${range(points.mapNotNull { it.fatigueLevel?.toFloat() })}"
            )
            appendLine("逐点（按时间正序）：")
            points.takeLast(60).forEach { point ->
                appendLine(pointLine(point))
            }
            if (points.size > 60) appendLine("（仅列出最近 60 条，其余数据已参与上面的汇总）")
        }.trim()
    }

    private suspend fun queryAlerts(args: JsonObject): String {
        if (LocalMode.ENABLED) return "当前是本地演示模式，没有告警记录。"
        val hours = args.intOr("hours", 24).coerceIn(1, 168)
        val unresolvedOnly = args["unresolved_only"]?.jsonPrimitive?.booleanOrNull ?: false
        val since = System.currentTimeMillis() - hours * 3_600_000L
        val alerts = ApiClient.auraApi.getAlerts().data.orEmpty()
            .filter { alert -> parseInstant(alert.createdAt)?.toEpochMilli()?.let { it >= since } ?: true }
            .filter { !unresolvedOnly || !it.isResolved }
            .sortedByDescending { it.createdAt }
            .take(20)
        if (alerts.isEmpty()) return "近 $hours 小时没有${if (unresolvedOnly) "未处理的" else ""}告警。"
        return buildString {
            appendLine("近 $hours 小时共 ${alerts.size} 条${if (unresolvedOnly) "未处理" else ""}告警（最多列出 20 条）：")
            alerts.forEach { alert ->
                appendLine(
                    "· [${severityLabel(alert.severity)}] ${localTime(alert.createdAt)} " +
                        "${alert.message}（${if (alert.isResolved) "已处理" else "未处理"}）"
                )
            }
        }.trim()
    }

    private suspend fun queryDevices(): String {
        if (LocalMode.ENABLED) return "当前是本地演示模式，没有真实设备。"
        val devices = ApiClient.auraApi.getDevices().data.orEmpty()
        if (devices.isEmpty()) return "当前没有登记设备。"
        val online = devices.count { it.status == "online" }
        return buildString {
            appendLine("共 ${devices.size} 台设备，在线 $online 台：")
            devices.forEach { device ->
                val room = device.roomName?.takeIf { it.isNotBlank() }?.let { "（$it）" } ?: ""
                val seen = device.lastSeenAt.takeIf { it.isNotBlank() }?.let { "，最后上报 ${localTime(it)}" } ?: ""
                appendLine("· ${device.name}$room ${if (device.status == "online") "在线" else "离线"}$seen")
            }
        }.trim()
    }

    private suspend fun queryLive(): String {
        if (LocalMode.ENABLED) return "当前是本地演示模式，没有实时链路。"
        val snapshot = ApiClient.auraApi.getLiveSnapshot().data
            ?: return "暂时读不到实时快照。"
        return buildString {
            appendLine(
                "链路：${if (snapshot.running) "运行中" else "未运行"}，累计 ${snapshot.frames} 帧，" +
                    "错误 ${snapshot.errors}，丢帧 ${snapshot.lost}，采样率 ${snapshot.fs} Hz"
            )
            val frame = snapshot.latest
            if (frame == null) {
                appendLine("当帧：算法尚未输出。")
            } else {
                appendLine(
                    "当帧：${if (frame.presence == 1) "检测到人" else "未检测到人"}，" +
                        "体动 ${if (frame.motion == 1) "有" else "无"}（强度 ${ratio(frame.motionLevel)}），" +
                        "置信度 ${ratio(frame.conf)}，相干性 ${ratio(frame.coh)}"
                )
                appendLine(
                    "心率估计：融合 ${fmt(frame.hrFused ?: frame.hr)}｜FFT ${fmt(frame.hrFft)}｜" +
                        "ACF ${fmt(frame.hrAcf)}｜PPI ${fmt(frame.hrPpi)}（来源 ${frame.ppiSource}），" +
                        "呼吸 ${fmt(frame.respHz?.let { it * 60.0 })} 次/分"
                )
            }
        }.trim()
    }

    private fun pointLine(point: VitalsPoint): String = buildString {
        append("${localTime(point.time)} 心率 ${fmt(point.heartRate?.toFloat())}")
        append(" 呼吸 ${fmt(point.respirationRate?.toFloat())}")
        append(" 专注 ${fmt(point.focusScore.toFloat())}")
        append(" 压力 ${fmt(point.stressLevel?.toFloat())}")
        append(" 疲劳 ${fmt(point.fatigueLevel?.toFloat())}")
    }

    private fun range(values: List<Float>): String {
        if (values.isEmpty()) return "无有效值"
        return "平均 ${fmt(values.average().toFloat())}（${fmt(values.min())}–${fmt(values.max())}）"
    }

    private fun JsonObject.intOr(key: String, fallback: Int): Int =
        this[key]?.jsonPrimitive?.intOrNull ?: fallback

    private fun function(
        name: String,
        description: String,
        properties: JsonObjectBuilderScope.() -> Unit
    ): JsonObject {
        val scope = JsonObjectBuilderScope().apply(properties)
        return buildJsonObject {
            put("type", "function")
            putJsonObject("function") {
                put("name", name)
                put("description", description)
                putJsonObject("parameters") {
                    put("type", "object")
                    put("properties", scope.build())
                }
            }
        }
    }

    private class JsonObjectBuilderScope {
        private val builder = mutableListOf<Pair<String, JsonObject>>()

        fun number(name: String, description: String) {
            builder += name to buildJsonObject {
                put("type", "integer")
                put("description", description)
            }
        }

        fun bool(name: String, description: String) {
            builder += name to buildJsonObject {
                put("type", "boolean")
                put("description", description)
            }
        }

        fun build(): JsonObject = buildJsonObject {
            builder.forEach { (key, value) -> put(key, value) }
        }
    }

    private fun severityLabel(severity: String): String = when (severity.lowercase()) {
        "high" -> "高"
        "medium" -> "中"
        "low" -> "低"
        else -> severity
    }

    private fun localTime(raw: String?): String {
        if (raw.isNullOrBlank()) return "—"
        val instant = parseInstant(raw) ?: return raw
        val zone = ZoneId.systemDefault()
        val time = instant.atZone(zone)
        val pattern = if (time.toLocalDate() == LocalDate.now(zone)) "HH:mm" else "MM-dd HH:mm"
        return time.format(DateTimeFormatter.ofPattern(pattern))
    }

    private fun parseInstant(raw: String?): Instant? {
        if (raw.isNullOrBlank()) return null
        runCatching { return Instant.parse(raw) }
        runCatching { return OffsetDateTime.parse(raw).toInstant() }
        runCatching {
            return LocalDateTime.parse(raw)
                .atZone(ZoneId.systemDefault()).toInstant()
        }
        runCatching {
            return LocalDateTime.parse(
                raw,
                DateTimeFormatter.ofPattern("yyyy-MM-dd HH:mm:ss")
            ).atZone(ZoneId.systemDefault()).toInstant()
        }
        return null
    }

    private fun fmt(v: Float?): String = v?.let { String.format("%.0f", it) } ?: "—"

    private fun fmt(v: Double?): String = v?.let { String.format("%.0f", it) } ?: "—"

    private fun ratio(v: Double?): String = v?.let { String.format("%.2f", it) } ?: "—"
}
