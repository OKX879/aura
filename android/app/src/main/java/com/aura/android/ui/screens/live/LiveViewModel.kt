package com.aura.android.ui.screens.live

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.aura.android.data.LiveMonitor
import com.aura.android.data.LocalMode
import com.aura.android.data.api.ApiClient
import com.aura.android.data.api.ApiResult
import com.aura.android.data.api.toApiResult
import com.aura.android.data.model.LiveSnapshot
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch

// 实时监测状态
class LiveViewModel : ViewModel() {

    private companion object {
        // 历史样本长度
        const val HISTORY_LEN = 48

        // 演示用快照
        val DEMO_SNAPSHOT = LiveSnapshot(
            source = "demo",
            running = false,
            fs = 100.0,
            channels = 8,
            bins = 16,
            latest = null
        )
    }

    var uiState by mutableStateOf<ApiResult<LiveSnapshot>>(ApiResult.Loading)
        private set

    var hostFps by mutableStateOf<Double?>(null)
        private set

    var radarFps by mutableStateOf<Double?>(null)
        private set

    var lastUpdatedAt by mutableStateOf<Long?>(null)
        private set

    // 最近心率样本
    var hrHistory by mutableStateOf<List<Float>>(emptyList())
        private set

    // 评分历史数据
    var stressHistory by mutableStateOf<List<Float>>(emptyList())
        private set
    var fatigueHistory by mutableStateOf<List<Float>>(emptyList())
        private set
    var focusHistory by mutableStateOf<List<Float>>(emptyList())
        private set

    // 轮询失败标记
    var stale by mutableStateOf(false)
        private set

    private var lastFrames: Long? = null
    private var lastRadarFrame: Long? = null
    private var lastSampleAt = 0L
    private var lastScoreStamp = 0L

    init {
        if (LocalMode.ENABLED) {
            // 本地模式使用内置曲线
            uiState = ApiResult.Success(DEMO_SNAPSHOT)
            seedDemoSeries()
            viewModelScope.launch {
                while (isActive) {
                    delay(1_000)
                    rollDemoSeries()
                }
            }
        } else {
            LiveMonitor.ensureStarted()
            viewModelScope.launch {
                load()
                while (isActive) {
                    delay(1_000)
                    load()
                }
            }
        }
    }

    fun load() {
        viewModelScope.launch {
            try {
                val response = ApiClient.auraApi.getLiveSnapshot()
                val result = response.toApiResult()
                if (result is ApiResult.Success) {
                    val snapshot = result.data
                    val now = System.currentTimeMillis()
                    val radarFrame = snapshot.latest?.radarFrame
                    val previousFrames = lastFrames
                    val previousRadarFrame = lastRadarFrame
                    val previousAt = lastSampleAt
                    val seconds = if (previousAt > 0) (now - previousAt) / 1000.0 else 0.0

                    if (previousFrames != null && seconds in 0.2..30.0) {
                        hostFps = (snapshot.frames - previousFrames) / seconds
                        if (radarFrame != null && previousRadarFrame != null) {
                            radarFps = (radarFrame - previousRadarFrame) / seconds
                        }
                    }

                    // 评分按采样时间戳追加
                    val derived = LiveMonitor.derive(snapshot.latest)
                    val hr = derived.hr
                    if (hr != null) {
                        hrHistory = (hrHistory + hr).takeLast(HISTORY_LEN)
                    }
                    val scores = LiveMonitor.scores.value
                    if (scores.updatedAtMs != 0L && scores.updatedAtMs != lastScoreStamp) {
                        lastScoreStamp = scores.updatedAtMs
                        scores.stress?.let {
                            stressHistory = (stressHistory + it).takeLast(HISTORY_LEN)
                        }
                        scores.fatigue?.let {
                            fatigueHistory = (fatigueHistory + it).takeLast(HISTORY_LEN)
                        }
                        scores.focus?.let {
                            focusHistory = (focusHistory + it).takeLast(HISTORY_LEN)
                        }
                    }
                    // 无有效心率时保留上一帧

                    lastFrames = snapshot.frames
                    lastRadarFrame = radarFrame
                    lastSampleAt = now
                    lastUpdatedAt = now
                    stale = false
                    uiState = result
                } else {
                    uiState = result
                }
            } catch (e: Exception) {
                stale = true
                if (uiState !is ApiResult.Success) {
                    uiState = ApiResult.Error(e.message ?: "无法连接实时数据")
                }
            }
        }
    }

    // 填充演示曲线
    private fun seedDemoSeries() {
        val seed = System.currentTimeMillis()
        hrHistory = demoHr(seed, HISTORY_LEN)
        stressHistory = demoStress(seed, HISTORY_LEN)
        fatigueHistory = demoFatigue(seed, HISTORY_LEN)
        focusHistory = demoFocus(seed, HISTORY_LEN)
    }

    private fun rollDemoSeries() {
        // 按秒级时间戳计算相位
        val phase = (System.currentTimeMillis() / 1000L % 1000L).toFloat()
        hrHistory = (hrHistory.drop(1) + (68f + 6f * kotlin.math.sin(phase * 0.35f) +
            2f * kotlin.math.sin(phase * 0.9f))).takeLast(HISTORY_LEN)
        stressHistory = (stressHistory.drop(1) +
            (46f + 12f * kotlin.math.sin(phase * 0.22f + 0.6f)).coerceIn(0f, 100f))
            .takeLast(HISTORY_LEN)
        fatigueHistory = (fatigueHistory.drop(1) +
            (36f + 7f * kotlin.math.sin(phase * 0.13f)).coerceIn(0f, 100f))
            .takeLast(HISTORY_LEN)
        focusHistory = (focusHistory.drop(1) +
            (80f - 11f * kotlin.math.sin(phase * 0.19f)).coerceIn(0f, 100f))
            .takeLast(HISTORY_LEN)
    }

    private fun demoHr(seed: Long, n: Int): List<Float> = (0 until n).map { i ->
        val x = i.toFloat()
        68f + 6f * kotlin.math.sin(x * 0.26f) + 2f * kotlin.math.sin(x * 0.75f)
    }

    private fun demoStress(seed: Long, n: Int): List<Float> = (0 until n).map { i ->
        val x = i.toFloat()
        (46f + 12f * kotlin.math.sin(x * 0.18f + 0.6f)).coerceIn(0f, 100f)
    }

    private fun demoFatigue(seed: Long, n: Int): List<Float> = (0 until n).map { i ->
        val x = i.toFloat()
        (36f + 7f * kotlin.math.sin(x * 0.11f)).coerceIn(0f, 100f)
    }

    private fun demoFocus(seed: Long, n: Int): List<Float> = (0 until n).map { i ->
        val x = i.toFloat()
        (80f - 11f * kotlin.math.sin(x * 0.15f)).coerceIn(0f, 100f)
    }
}
