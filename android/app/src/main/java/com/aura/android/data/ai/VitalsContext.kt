package com.aura.android.data.ai

import com.aura.android.data.DemoData
import com.aura.android.data.LocalMode
import com.aura.android.data.api.ApiClient
import com.aura.android.data.api.ApiResult
import com.aura.android.data.api.toApiResult
import com.aura.android.data.model.MobileDashboard
import com.aura.android.data.model.VitalsPoint

// 送给模型的体征快照
data class VitalsSnapshot(
    val userName: String,
    val todaySummary: com.aura.android.data.model.TodaySummary,
    val latest: com.aura.android.data.model.FocusRecord?,
    val vitals: List<VitalsPoint>,
    val unresolvedAlerts: Int,
    val alertMessages: List<String>,
    val onlineDevices: Int,
    val totalDevices: Int
) {

    private val recentVitals: List<VitalsPoint> get() = vitals.takeLast(12)

    private fun avg(values: List<Float>): Float? =
        if (values.isEmpty()) null else values.average().toFloat()

    val currentHeartRate: Float?
        get() = latest?.heartRate?.toFloat()
            ?: vitals.lastOrNull { it.heartRate != null }?.heartRate?.toFloat()

    val currentRespiration: Float?
        get() = latest?.respirationRate?.toFloat()
            ?: vitals.lastOrNull { it.respirationRate != null }?.respirationRate?.toFloat()

    val currentStress: Float?
        get() = latest?.stressLevel?.toFloat()
            ?: vitals.lastOrNull { it.stressLevel != null }?.stressLevel?.toFloat()

    val currentFatigue: Float?
        get() = latest?.fatigueLevel?.toFloat()
            ?: vitals.lastOrNull { it.fatigueLevel != null }?.fatigueLevel?.toFloat()

    val currentFocus: Float?
        get() = latest?.focusScore?.toFloat()
            ?: vitals.lastOrNull()?.focusScore?.toFloat()

    // 心率波动幅度
    val heartRateSwing: Float?
        get() {
            val values = recentVitals.mapNotNull { it.heartRate?.toFloat() }
            if (values.size < 2) return null
            return values.max() - values.min()
        }

    val stressTrend: String
        get() {
            val values = recentVitals.mapNotNull { it.stressLevel?.toFloat() }
            if (values.size < 4) return "样本不足"
            val first = values.take(values.size / 2).average()
            val second = values.drop(values.size / 2).average()
            val delta = second - first
            return when {
                delta > 4 -> "上升"
                delta < -4 -> "下降"
                else -> "平稳"
            }
        }

    fun focusAverage(): Float? = avg(recentVitals.map { it.focusScore.toFloat() })

    fun toPrompt(): String = buildString {
        appendLine("【AURA 雷达体征数据】")
        appendLine("当前时间：${java.time.ZonedDateTime.now().format(java.time.format.DateTimeFormatter.ofPattern("yyyy-MM-dd HH:mm EEEE", java.util.Locale.CHINA))}（设备本地时区）")
        appendLine("用户：$userName")
        appendLine(
            "最新一帧：心率 ${fmt(currentHeartRate)} 次/分，呼吸 ${fmt(currentRespiration)} 次/分，" +
                "专注 ${fmt(currentFocus)} 分，压力 ${fmt(currentStress)} 分，疲劳 ${fmt(currentFatigue)} 分"
        )
        appendLine(
            "今日汇总：记录 ${todaySummary.recordCount} 条，平均心率 ${fmt(todaySummary.averageHeartRate?.toFloat())}" +
                "（${fmt(todaySummary.minHeartRate?.toFloat())}–${fmt(todaySummary.maxHeartRate?.toFloat())}），" +
                "平均呼吸 ${fmt(todaySummary.averageRespirationRate?.toFloat())}，" +
                "平均专注 ${fmt(todaySummary.averageFocus.toFloat())}（最高 ${fmt(todaySummary.maxFocus.toFloat())} / " +
                "最低 ${fmt(todaySummary.minFocus.toFloat())}），平均压力 ${fmt(todaySummary.averageStress?.toFloat())}，" +
                "平均疲劳 ${fmt(todaySummary.averageFatigue?.toFloat())}"
        )
        appendLine(
            "近 ${recentVitals.size} 个采样点：心率波动 ${fmt(heartRateSwing)}，" +
                "专注均值 ${fmt(focusAverage())}，压力趋势 $stressTrend"
        )
        appendLine("设备：在线 $onlineDevices / 共 $totalDevices")
        appendLine("未处理告警：$unresolvedAlerts 条${if (alertMessages.isEmpty()) "" else "（" + alertMessages.joinToString("；") + "）"}")
        appendLine("数据口径：全部为毫米波雷达非接触测量，心率静息基线约 68 次/分。")
    }

    private fun fmt(v: Float?): String = v?.let { String.format("%.0f", it) } ?: "无有效值"
}

object VitalsContext {

    // 读取当前仪表盘数据
    suspend fun load(): VitalsSnapshot? {
        val dashboard: MobileDashboard = if (LocalMode.ENABLED) {
            DemoData.dashboard
        } else {
            val result = runCatching { ApiClient.auraApi.getDashboard().toApiResult() }.getOrNull()
            (result as? ApiResult.Success)?.data ?: DemoData.dashboard
        }
        return VitalsSnapshot(
            userName = dashboard.user.name,
            todaySummary = dashboard.todaySummary,
            latest = dashboard.latestRecord,
            vitals = dashboard.vitalsHistory,
            unresolvedAlerts = dashboard.alertSummary.unresolvedCount,
            alertMessages = dashboard.alertSummary.recent.take(3).map { it.message },
            onlineDevices = dashboard.deviceSummary.online,
            totalDevices = dashboard.deviceSummary.total
        )
    }

    // 系统提示词
    fun systemPrompt(snapshot: VitalsSnapshot): String = buildString {
        appendLine("你是 AURA 健康助手，既能解读毫米波雷达采集的体征数据，也能回答一般的健康、生活与常识问题。")
        appendLine("回答要求：")
        appendLine("1. 涉及“我/我的”体征数值时，以给定快照为准，不要编造；需要补充通用医学或生活知识时，可以自然展开。")
        appendLine("2. 回答结构自己决定，不必套固定模板，长度按问题需要；尽量先给直接答案。")
        appendLine("3. 遇到新闻、实时数据、价格、政策、赛事、人物近况等超出知识范围的问题，先调用 web_search 再回答，并在末尾注明信息来源。检索只能通过工具调用发起，不要把调用标记或 DSML 写进正文。")
        appendLine("4. 语气专业克制，不做疾病诊断；提示异常时说明这只是趋势提示，确诊需就医。")
        appendLine("5. 输出纯文本，可用换行和短横线分点，但不要使用 Markdown 星号或井号。")
        appendLine()
        appendLine(snapshot.toPrompt())
    }

    // 本地分析回答
    fun localAnalysis(question: String, snapshot: VitalsSnapshot): String {
        val q = question.trim()
        fun hrLine(): String {
            val hr = snapshot.currentHeartRate
            val avg = snapshot.todaySummary.averageHeartRate?.toFloat()
            val swing = snapshot.heartRateSwing
            val status = when {
                hr == null -> "暂无有效心率"
                hr < 50 -> "偏低"
                hr <= 90 -> "处于常见静息区间"
                else -> "偏高"
            }
            return buildString {
                append("当前心率 ${fmt(hr)} 次/分，$status")
                if (avg != null) append("；今日平均 ${fmt(avg)} 次/分")
                if (swing != null) append("，近期波动 ${fmt(swing)} 次/分")
                append("。")
            }
        }

        return when {
            q.contains("心率") || q.contains("心跳") || q.contains("bpm", true) ->
                buildString {
                    appendLine(hrLine())
                    appendLine("依据：雷达按 PPI 间期融合估算心率，静息基线约 68 次/分。")
                    appendLine("建议：若持续偏离基线 10 次/分以上并伴随不适，建议用指夹血氧仪复核。")
                }.trim()

            q.contains("压力") || q.contains("焦虑") ->
                buildString {
                    val s = snapshot.currentStress
                    val level = when {
                        s == null -> "无有效值"
                        s < 35 -> "偏低"
                        s < 60 -> "中等"
                        else -> "偏高"
                    }
                    appendLine("当前压力 ${fmt(s)} 分，属于「$level」水平，近期趋势${snapshot.stressTrend}。")
                    appendLine("依据：压力由心率变异性、体动幅度与呼吸节律联合估算。")
                    appendLine("建议：压力偏高时做 3 分钟缓慢腹式呼吸（吸气 4 秒、呼气 6 秒），并减少连续久坐。")
                }.trim()

            q.contains("疲劳") || q.contains("累") || q.contains("睡眠") ->
                buildString {
                    appendLine("当前疲劳 ${fmt(snapshot.currentFatigue)} 分，今日平均 ${fmt(snapshot.todaySummary.averageFatigue?.toFloat())} 分。")
                    appendLine("依据：疲劳主要来自心率漂移与呼吸变浅的持续时长。")
                    appendLine("建议：每 90 分钟起身活动一次，午后安排 10–20 分钟闭眼休息。")
                }.trim()

            q.contains("建议") || q.contains("怎么办") || q.contains("改善") ->
                buildString {
                    appendLine("基于当前数据的三条建议：")
                    appendLine("1. ${hrLine()}")
                    appendLine("2. 专注均值 ${fmt(snapshot.focusAverage())} 分，压力趋势${snapshot.stressTrend}，建议用「专注 25 分钟 + 休息 5 分钟」的节奏。")
                    appendLine("3. 今日平均呼吸 ${fmt(snapshot.todaySummary.averageRespirationRate?.toFloat())} 次/分，若低于 12 次/分请留意测量距离与坐姿。")
                }.trim()

            q.contains("总结") || q.contains("状态") || q.contains("怎么样") ->
                buildString {
                    appendLine("${snapshot.userName}，当前状态概览：")
                    appendLine("· 心率 ${fmt(snapshot.currentHeartRate)} 次/分，呼吸 ${fmt(snapshot.currentRespiration)} 次/分")
                    appendLine("· 专注 ${fmt(snapshot.currentFocus)} 分，压力 ${fmt(snapshot.currentStress)} 分，疲劳 ${fmt(snapshot.currentFatigue)} 分")
                    appendLine("· 今日已记录 ${snapshot.todaySummary.recordCount} 条，未处理告警 ${snapshot.unresolvedAlerts} 条")
                    append("整体来看，各项指标${if ((snapshot.currentStress ?: 0f) < 60f) "处于可接受范围" else "提示压力偏高，建议优先休息"}。")
                }.trim()

            else ->
                buildString {
                    appendLine("我当前读到的最新数据是：${hrLine()}")
                    appendLine("专注 ${fmt(snapshot.currentFocus)} 分，压力 ${fmt(snapshot.currentStress)} 分，疲劳 ${fmt(snapshot.currentFatigue)} 分，呼吸 ${fmt(snapshot.currentRespiration)} 次/分。")
                    append("你可以直接问「我的心率怎么样」「压力分析」「给我一些健康建议」或「我的状态总结」，我会结合这些数值逐项解释。")
                }.trim()
        }
    }

    private fun fmt(v: Float?): String = v?.let { String.format("%.0f", it) } ?: "无有效值"
}
