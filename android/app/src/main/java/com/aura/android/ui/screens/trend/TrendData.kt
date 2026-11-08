package com.aura.android.ui.screens.trend

import com.aura.android.data.LocalMode
import kotlin.math.sin

// 趋势页数据
object TrendData {

    const val YEAR_LABEL = "2026"

    private fun series(days: Int, base: Float, amp: Float, phase: Float): List<Float> =
        (0 until days).map { i ->
            val x = i.toFloat()
            (base + amp * sin(x * 0.42f + phase) +
                amp * 0.45f * sin(x * 1.13f + phase * 0.7f)).coerceIn(0f, 100f)
        }

    // 本周 7 天数据
    val week: List<Float> = series(7, 76f, 11f, 0.4f)

    // 上周 7 天数据
    val lastWeek: List<Float> = series(7, 68f, 9f, 1.9f)

    // 本月 30 天数据
    val month: List<Float> = series(30, 74f, 12f, 0.9f)

    val weekLabels = listOf("一", "二", "三", "四", "五", "六", "日")

    val monthLabels = listOf("1 日", "8 日", "15 日", "22 日", "30 日")

    fun weekAverage(): Float = week.average().toFloat()

    fun lastWeekAverage(): Float = lastWeek.average().toFloat()

    fun monthAverage(): Float = month.average().toFloat()

    fun delta(): Float = weekAverage() - lastWeekAverage()

    fun rms(): Float {
        if (!LocalMode.ENABLED) return 0f
        val diff = week.indices.sumOf { i ->
            val d = (week[i] - lastWeek[i]).toDouble()
            d * d
        } / week.size
        return kotlin.math.sqrt(diff).toFloat()
    }
}
