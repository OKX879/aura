package com.aura.android.data

import com.aura.android.data.api.ApiClient
import com.aura.android.data.model.Alert
import com.aura.android.data.model.LiveLatest
import com.aura.android.data.model.LiveSnapshot
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import kotlin.math.abs

// 实时监测数据源
object LiveMonitor {

    // 派生的工程评分
    data class Derived(
        val hr: Float? = null,
        val resp: Float? = null,
        val stress: Float? = null,
        val fatigue: Float? = null,
        val focus: Float? = null
    )

    // 三项评分数值
    data class Scores(
        val stress: Float? = null,
        val fatigue: Float? = null,
        val focus: Float? = null,
        val updatedAtMs: Long = 0L
    )

    private const val RESTING_BASELINE_BPM = 68f
    private const val OFFLINE_FAILURES = 3
    private const val SCORE_INTERVAL_MS = 10 * 60 * 1000L

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)

    @Volatile private var started = false

    private val _snapshot = MutableStateFlow<LiveSnapshot?>(null)
    val snapshot: StateFlow<LiveSnapshot?> = _snapshot

    private val _derived = MutableStateFlow(Derived())
    val derived: StateFlow<Derived> = _derived

    private val _scores = MutableStateFlow(Scores())
    val scores: StateFlow<Scores> = _scores

    private val _alerts = MutableStateFlow<List<Alert>>(emptyList())
    val alerts: StateFlow<List<Alert>> = _alerts

    private val _online = MutableStateFlow(false)
    val online: StateFlow<Boolean> = _online

    private class RuleState {
        var sinceMs = 0L
        var clearSinceMs = 0L
        var alert: Alert? = null
    }

    private val rules = mutableMapOf<String, RuleState>()

    fun ensureStarted() {
        if (LocalMode.ENABLED || started) return
        synchronized(this) {
            if (started) return
            started = true
        }
        scope.launch { loop() }
    }

    // 换算体征与评分
    fun derive(latest: LiveLatest?): Derived {
        val hr = latest?.hr?.takeIf { it > 0.0 }?.toFloat()
        val resp = latest?.respHz?.takeIf { it > 0.0 }?.let { (it * 60.0).toFloat() }
        if (hr == null) return Derived(hr = null, resp = resp)
        val delta = hr - RESTING_BASELINE_BPM
        val motion = latest.motionLevel?.toFloat() ?: 0f
        val coherence = latest.coh?.toFloat() ?: 0f
        val confidence = latest.conf?.toFloat() ?: 0f
        val stress = (48f + delta * 1.8f + motion * 34f +
            (1f - coherence.coerceIn(0f, 1f)) * 18f).coerceIn(0f, 100f)
        val fatigue = (30f + delta * 1.1f + motion * 12f).coerceIn(0f, 100f)
        val focus = (86f - abs(delta) * 1.2f - motion * 46f -
            (1f - confidence.coerceIn(0f, 1f)) * 22f).coerceIn(0f, 100f)
        return Derived(hr, resp, stress, fatigue, focus)
    }

    private suspend fun loop() {
        var failures = 0
        var lastFrames = -1L
        var framesChangedAt = System.currentTimeMillis()
        while (scope.isActive) {
            try {
                val snapshot = ApiClient.auraApi.getLiveSnapshot().data
                if (snapshot == null) {
                    failures++
                } else {
                    failures = 0
                    val now = System.currentTimeMillis()
                    if (snapshot.frames != lastFrames) {
                        lastFrames = snapshot.frames
                        framesChangedAt = now
                    }
                    _online.value = true
                    _snapshot.value = snapshot
                    val derived = derive(snapshot.latest)
                    _derived.value = derived
                    refreshScores(derived, now)
                    evaluate(snapshot, now - framesChangedAt)
                }
            } catch (e: Exception) {
                failures++
            }
            if (failures >= OFFLINE_FAILURES) {
                _online.value = false
                raise("device_offline", "high",
                    "设备离线：连续 $failures 秒无法连接雷达服务")
            } else if (failures == 0) {
                clear("device_offline")
            }
            delay(1_000)
        }
    }

    // 每 10 分钟刷新评分
    private fun refreshScores(derived: Derived, now: Long) {
        if (derived.stress == null || derived.fatigue == null || derived.focus == null) return
        val current = _scores.value
        if (current.updatedAtMs != 0L && now - current.updatedAtMs < SCORE_INTERVAL_MS) return
        _scores.value = Scores(
            stress = derived.stress,
            fatigue = derived.fatigue,
            focus = derived.focus,
            updatedAtMs = now
        )
    }

    private fun evaluate(snapshot: LiveSnapshot, stalledMs: Long) {
        val latest = snapshot.latest
        val derived = _derived.value
        val hr = derived.hr
        val presence = latest?.presence == 1

        apply("radar_stalled", "medium",
            snapshot.running && stalledMs >= 5_000,
            0,
            "雷达数据已停止更新（${(stalledMs / 1000).coerceAtLeast(1)} 秒无新帧）")

        apply("no_presence", "low",
            latest != null && !presence,
            30_000,
            "未检测到人员，生命体征暂停更新")

        apply("hr_high", "high",
            hr != null && hr >= 100f,
            10_000,
            "心率偏高：${hr?.toInt()} BPM")

        apply("hr_low", "high",
            hr != null && presence && hr < 45f,
            10_000,
            "心率偏低：${hr?.toInt()} BPM")

        apply("hr_elevated", "medium",
            hr != null && hr >= 90f && hr < 100f,
            30_000,
            "心率持续偏高：${hr?.toInt()} BPM")

        apply("motion", "low",
            latest?.motion == 1 && (latest.motionLevel ?: 0.0) > 0.5,
            10_000,
            "检测到持续体动，心率读数可能不稳定")

        apply("weak_signal", "low",
            latest != null && hr != null &&
                (latest.conf ?: 0.0) < 0.3 && latest.confident == 0,
            30_000,
            "雷达信号偏弱，读数置信度低")
    }

    private fun apply(
        type: String,
        severity: String,
        active: Boolean,
        holdMs: Long,
        message: String
    ) {
        val now = System.currentTimeMillis()
        val state = rules.getOrPut(type) { RuleState() }
        if (active) {
            state.clearSinceMs = 0L
            if (state.sinceMs == 0L) state.sinceMs = now
            if (state.alert == null) {
                if (now - state.sinceMs >= holdMs) {
                    state.alert = Alert(
                        id = "live-$type",
                        type = type,
                        severity = severity,
                        message = message,
                        isResolved = false,
                        createdAt = stamp(now)
                    )
                    state.alert?.let(AlertHistory::record)
                    publish()
                }
            } else if (state.alert?.message != message) {
                state.alert = state.alert?.copy(message = message)
                publish()
            }
        } else {
            if (state.alert == null) {
                state.sinceMs = 0L
            } else {
                if (state.clearSinceMs == 0L) state.clearSinceMs = now
                if (now - state.clearSinceMs >= 5_000) {
                    rules.remove(type)
                    publish()
                }
            }
        }
    }

    private fun raise(type: String, severity: String, message: String) {
        val state = rules.getOrPut(type) { RuleState() }
        if (state.alert == null) {
            state.alert = Alert(
                id = "live-$type",
                type = type,
                severity = severity,
                message = message,
                isResolved = false,
                createdAt = stamp(System.currentTimeMillis())
            )
            state.alert?.let(AlertHistory::record)
            publish()
        } else if (state.alert?.message != message) {
            state.alert = state.alert?.copy(message = message)
            publish()
        }
    }

    private fun clear(type: String) {
        if (rules.remove(type) != null) publish()
    }

    // 忽略一条实时告警
    fun dismiss(type: String) {
        if (rules.remove(type) != null) publish()
    }

    private fun publish() {
        _alerts.value = rules.values
            .mapNotNull { it.alert }
            .sortedWith(compareByDescending<Alert> { severityRank(it.severity) }
                .thenByDescending { it.createdAt })
    }

    private fun severityRank(severity: String): Int = when (severity) {
        "high" -> 2
        "medium" -> 1
        else -> 0
    }

    private fun stamp(epochMs: Long): String =
        SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.getDefault()).format(Date(epochMs))
}
