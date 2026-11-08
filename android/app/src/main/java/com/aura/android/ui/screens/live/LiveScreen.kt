package com.aura.android.ui.screens.live

import androidx.compose.animation.AnimatedContent
import androidx.compose.animation.core.LinearEasing
import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.keyframes
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.tween
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.slideInVertically
import androidx.compose.animation.slideOutVertically
import androidx.compose.animation.togetherWith
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Favorite
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.drawscope.rotate
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aura.android.data.api.ApiResult
import com.aura.android.data.model.LiveLatest
import com.aura.android.data.model.LiveSnapshot
import com.aura.android.ui.components.AnimatedCounter
import com.aura.android.ui.components.AuraCard
import com.aura.android.ui.components.LieBarChart
import com.aura.android.ui.components.LieCard
import com.aura.android.ui.components.LieEyebrow
import com.aura.android.ui.components.LieGauge
import com.aura.android.ui.components.LieReveal
import com.aura.android.ui.components.LieStatusDot
import com.aura.android.ui.components.LieTrendChart
import com.aura.android.ui.theme.ChartData
import com.aura.android.ui.theme.ChartHero
import com.aura.android.ui.theme.ErrorRed
import com.aura.android.ui.theme.Hairline
import com.aura.android.ui.theme.Hint
import com.aura.android.ui.theme.Muted
import com.aura.android.ui.theme.OnSurface
import com.aura.android.ui.theme.Paper
import com.aura.android.ui.theme.PrimaryBlue
import com.aura.android.ui.theme.PrimaryBlueSoft
import com.aura.android.ui.theme.Success
import com.aura.android.ui.theme.Surface
import com.aura.android.ui.theme.Warning
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import kotlin.math.min

private val LiveBackground = Surface
private val LivePanel = Paper
private val LiveLine = Hairline
private val LiveText = OnSurface
private val LiveMuted = Muted

// 实时页
@Composable
fun LiveScreen(viewModel: LiveViewModel) {
    val scrollState = rememberScrollState()

    Scaffold(
        containerColor = LiveBackground,
        topBar = {
            LiveTopBar(
                running = (viewModel.uiState as? ApiResult.Success)?.data?.running == true,
                stale = viewModel.stale,
                updatedAt = viewModel.lastUpdatedAt,
                onRefresh = viewModel::load
            )
        }
    ) { padding ->
        when (val state = viewModel.uiState) {
            is ApiResult.Loading -> DarkLoading(Modifier.fillMaxSize().padding(padding))
            is ApiResult.Error -> DarkError(
                message = state.message,
                onRetry = viewModel::load,
                modifier = Modifier.fillMaxSize().padding(padding)
            )
            is ApiResult.Success -> {
                val snapshot = state.data
                val latest = snapshot.latest
                Column(
                    modifier = Modifier
                        .fillMaxSize()
                        .verticalScroll(scrollState)
                        .padding(padding)
                        .padding(horizontal = 20.dp),
                    verticalArrangement = Arrangement.spacedBy(14.dp)
                ) {
                    LieReveal(index = 0) {
                        HeartHero(
                            latest = latest,
                            history = viewModel.hrHistory,
                            running = snapshot.running
                        )
                    }

                    LieReveal(index = 1) {
                        // Y 轴按 5 BPM 取整
                        val (hrLo, hrHi) = hrAxisRange(viewModel.hrHistory)
                        LieCard {
                            LieEyebrow("心率 / HR · 最近 48 秒")
                            Spacer(modifier = Modifier.height(10.dp))
                            LieTrendChart(
                                values = viewModel.hrHistory,
                                modifier = Modifier
                                    .fillMaxWidth()
                                    .height(158.dp),
                                color = ChartData,
                                yMin = hrLo,
                                yMax = hrHi,
                                yTicks = listOf(hrLo, (hrLo + hrHi) / 2f, hrHi),
                                xLabels = listOf("-48s", "-32s", "-16s", "现在")
                            )
                        }
                    }

                    LieReveal(index = 2) {
                        LieCard {
                            LieEyebrow("评分 / SCORE · 0–100 · 每 10 分钟")
                            ScoreBarRow("压力", viewModel.stressHistory, ChartData, showAxis = false)
                            ScoreBarRow("疲劳", viewModel.fatigueHistory, Color(0xFF5A7049), showAxis = false)
                            ScoreBarRow("专注度", viewModel.focusHistory, ChartHero, showAxis = true)
                        }
                    }

                    Text(
                        text = "压力、疲劳、专注度每 10 分钟更新一次（每根柱 10 分钟），由心率相对个人基线的偏移、体动与信号相干性派生，是工程估计，不等同于 ECG，不用于医疗诊断。",
                        style = MaterialTheme.typography.labelSmall.copy(color = LiveMuted.copy(alpha = 0.7f)),
                        modifier = Modifier.padding(top = 2.dp, bottom = 112.dp)
                    )
                }
            }
        }
    }
}

// 计算心率 Y 轴范围
private fun hrAxisRange(values: List<Float>): Pair<Float, Float> {
    val lo = values.minOrNull() ?: 55f
    val hi = values.maxOrNull() ?: 85f
    var a = kotlin.math.floor((lo - 4f) / 5f) * 5f
    var b = kotlin.math.ceil((hi + 4f) / 5f) * 5f
    if (b - a < 20f) {
        val mid = (a + b) / 2f
        a = mid - 10f
        b = mid + 10f
    }
    return a.coerceAtLeast(30f) to b
}

@Composable
private fun HeartHero(latest: LiveLatest?, history: List<Float>, running: Boolean) {
    val displayHr = latest?.hr?.takeIf { it > 0 }?.toFloat() ?: history.lastOrNull()
    LieCard {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Column(modifier = Modifier.weight(1f)) {
                LieEyebrow("实时心率 / LIVE HR")
                Spacer(modifier = Modifier.height(12.dp))
                Row(verticalAlignment = Alignment.Bottom) {
                    AnimatedCounter(
                        value = displayHr,
                        style = MaterialTheme.typography.displayLarge.copy(fontSize = 64.sp),
                        color = OnSurface
                    )
                    Text(
                        text = "BPM",
                        style = MaterialTheme.typography.labelSmall.copy(color = Hint),
                        modifier = Modifier.padding(start = 6.dp, bottom = 10.dp)
                    )
                }
                Spacer(modifier = Modifier.height(14.dp))
                LieStatusDot(
                    color = if (running) Success else Hint,
                    text = if (running) "信号在线 · 每 1 秒更新" else "设备离线 · 等待数据",
                    pulse = running
                )
            }
            LieGauge(
                fraction = ((displayHr ?: 0f) / 120f).coerceIn(0f, 1f),
                modifier = Modifier.size(96.dp),
                color = ChartData
            )
        }
    }
}

// 一行评分柱状图
@Composable
private fun ScoreBarRow(
    label: String,
    values: List<Float>,
    color: Color,
    showAxis: Boolean
) {
    // 聚合数据避免过密
    val bars = if (values.size > 24) {
        values.chunked(3).map { chunk -> chunk.average().toFloat() }
    } else {
        values
    }
    Row(modifier = Modifier.fillMaxWidth()) {
        Text(
            text = label,
            style = MaterialTheme.typography.labelMedium.copy(color = LiveMuted),
            modifier = Modifier.width(52.dp).padding(top = 6.dp)
        )
        Box(
            modifier = Modifier
                .weight(1f)
                .height(96.dp)
        ) {
            LieBarChart(
                values = bars,
                modifier = Modifier.fillMaxSize(),
                color = color,
                highlightLast = true,
                yMax = 100f,
                yTicks = if (showAxis) listOf(0f, 50f, 100f) else emptyList(),
                xLabels = if (showAxis) listOf("-8h", "-4h", "现在") else emptyList()
            )
        }
        Text(
            text = values.lastOrNull()?.let { "${it.toInt()}" } ?: "--",
            style = MaterialTheme.typography.headlineMedium.copy(
                color = color,
                fontSize = 22.sp
            ),
            modifier = Modifier.width(40.dp).padding(top = 4.dp)
        )
    }
}

@Composable
private fun LiveTopBar(
    running: Boolean,
    stale: Boolean,
    updatedAt: Long?,
    onRefresh: () -> Unit
) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .background(
                Brush.verticalGradient(
                    listOf(
                        LiveBackground.copy(alpha = 1.00f),
                        LiveBackground.copy(alpha = 0.97f),
                        LiveBackground.copy(alpha = 0.90f)
                    )
                )
            )
            .statusBarsPadding()
            .padding(start = 20.dp, end = 12.dp, top = 10.dp, bottom = 14.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Column(modifier = Modifier.weight(1f)) {
            LieEyebrow("LIVE MONITOR")
            Spacer(modifier = Modifier.height(5.dp))
            Text(
                text = "实时",
                style = MaterialTheme.typography.displayLarge.copy(
                    color = LiveText,
                    fontSize = 38.sp
                )
            )
        }
        val (label, color) = when {
            stale -> "连接中断" to Warning
            running -> "在线" to Success
            else -> "离线" to ErrorRed
        }
        Text(
            text = label,
            style = MaterialTheme.typography.labelSmall.copy(
                color = color,
                fontWeight = FontWeight.Bold
            ),
            modifier = Modifier
                .clip(RoundedCornerShape(30.dp))
                .background(color.copy(alpha = 0.14f))
                .padding(horizontal = 10.dp, vertical = 5.dp)
        )
        IconButton(onClick = onRefresh) {
            Icon(Icons.Default.Refresh, contentDescription = "刷新", tint = LiveText)
        }
    }
}

@Composable
private fun RadarPanel(
    latest: LiveLatest?,
    history: List<Float>,
    stale: Boolean,
    running: Boolean
) {
    val transition = rememberInfiniteTransition(label = "radar")
    val sweep by transition.animateFloat(
        initialValue = 0f,
        targetValue = 360f,
        animationSpec = infiniteRepeatable(
            animation = tween(6000, easing = LinearEasing),
            repeatMode = RepeatMode.Restart
        ),
        label = "sweep"
    )
    val pulse by transition.animateFloat(
        initialValue = 0f,
        targetValue = 1f,
        animationSpec = infiniteRepeatable(
            animation = tween(2400, easing = LinearEasing),
            repeatMode = RepeatMode.Restart
        ),
        label = "pulse"
    )
    val beat by transition.animateFloat(
        initialValue = 1f,
        targetValue = 1.12f,
        animationSpec = infiniteRepeatable(
            animation = keyframes {
                durationMillis = 1000
                1.12f at 120
                1f at 380
                1f at 1000
            },
            repeatMode = RepeatMode.Restart
        ),
        label = "beat"
    )

    Card(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(22.dp),
        colors = CardDefaults.cardColors(containerColor = LivePanel),
        border = BorderStroke(1.dp, LiveLine),
        elevation = CardDefaults.cardElevation(defaultElevation = 0.dp)
    ) {
        Box(modifier = Modifier.fillMaxWidth().height(330.dp)) {
            Canvas(modifier = Modifier.fillMaxSize()) {
                val center = Offset(size.width / 2f, size.height / 2f - 10.dp.toPx())
                val radius = min(size.width, size.height) * 0.42f

                drawCircle(
                    color = PrimaryBlue.copy(alpha = 0.14f),
                    radius = radius,
                    center = center,
                    style = Stroke(width = 1.dp.toPx())
                )
                drawCircle(
                    color = PrimaryBlue.copy(alpha = 0.10f),
                    radius = radius * 0.66f,
                    center = center,
                    style = Stroke(width = 1.dp.toPx())
                )
                drawCircle(
                    color = PrimaryBlue.copy(alpha = 0.08f),
                    radius = radius * 0.33f,
                    center = center,
                    style = Stroke(width = 1.dp.toPx())
                )

                drawLine(
                    color = PrimaryBlue.copy(alpha = 0.08f),
                    start = Offset(center.x - radius, center.y),
                    end = Offset(center.x + radius, center.y),
                    strokeWidth = 1.dp.toPx()
                )
                drawLine(
                    color = PrimaryBlue.copy(alpha = 0.08f),
                    start = Offset(center.x, center.y - radius),
                    end = Offset(center.x, center.y + radius),
                    strokeWidth = 1.dp.toPx()
                )

                rotate(degrees = sweep, pivot = center) {
                    drawArc(
                        brush = Brush.sweepGradient(
                            0f to Color.Transparent,
                            0.35f to PrimaryBlue.copy(alpha = 0.30f),
                            0.5f to Color.Transparent,
                            center = center
                        ),
                        startAngle = -90f,
                        sweepAngle = 100f,
                        useCenter = true,
                        topLeft = Offset(center.x - radius, center.y - radius),
                        size = androidx.compose.ui.geometry.Size(radius * 2f, radius * 2f)
                    )
                }

                val pulseRadius = radius * 0.55f + radius * 0.45f * pulse
                drawCircle(
                    color = PrimaryBlue.copy(alpha = (1f - pulse) * 0.28f),
                    radius = pulseRadius,
                    center = center,
                    style = Stroke(width = 1.5.dp.toPx())
                )

                drawCircle(
                    color = PrimaryBlue.copy(alpha = 0.55f),
                    radius = 4.dp.toPx(),
                    center = center
                )

                drawSparkline(
                    values = history,
                    color = PrimaryBlue,
                    left = 18.dp.toPx(),
                    right = size.width - 18.dp.toPx(),
                    bottom = size.height - 14.dp.toPx(),
                    height = 54.dp.toPx()
                )
            }

            Column(
                modifier = Modifier
                    .align(Alignment.TopCenter)
                    .padding(top = 34.dp),
                horizontalAlignment = Alignment.CenterHorizontally
            ) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Icon(
                        imageVector = Icons.Default.Favorite,
                        contentDescription = null,
                        tint = PrimaryBlueSoft,
                        modifier = Modifier
                            .size(13.dp)
                            .graphicsLayer {
                                scaleX = beat
                                scaleY = beat
                            }
                    )
                    Spacer(modifier = Modifier.width(6.dp))
                    Text(
                        text = "HEART RATE",
                        style = MaterialTheme.typography.labelSmall.copy(color = LiveMuted)
                    )
                }
                Spacer(modifier = Modifier.height(8.dp))
                Row(verticalAlignment = Alignment.Bottom) {
                    AnimatedContent(
                        targetState = latest?.hr?.takeIf { it > 0 }?.let { "${it.toInt()}" } ?: "--",
                        transitionSpec = {
                            (fadeIn(tween(220)) + slideInVertically(tween(220)) { it / 3 })
                                .togetherWith(
                                    fadeOut(tween(140)) + slideOutVertically(tween(140)) { -it / 3 }
                                )
                        },
                        label = "heartRate"
                    ) { text ->
                        Text(
                            text = text,
                            style = MaterialTheme.typography.displayLarge.copy(
                                color = LiveText,
                                fontSize = 56.sp,
                                lineHeight = 60.sp,
                                fontWeight = FontWeight.SemiBold
                            )
                        )
                    }
                    Text(
                        text = "BPM",
                        style = MaterialTheme.typography.labelMedium.copy(color = LiveMuted),
                        modifier = Modifier.padding(bottom = 8.dp, start = 6.dp)
                    )
                }
                Spacer(modifier = Modifier.height(4.dp))
                Text(
                    text = buildString {
                        append("置信度 ")
                        append(latest?.conf?.let { "${(it * 100.0).toInt()}%" } ?: "--")
                        append(" · 信号 ")
                        append(latest?.coh?.let { "${(it * 100.0).toInt()}%" } ?: "--")
                    },
                    style = MaterialTheme.typography.labelSmall.copy(color = LiveMuted)
                )
                Spacer(modifier = Modifier.height(10.dp))
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    DarkChip(
                        text = if (latest?.presence == 1) "检测到有人" else "无人",
                        color = if (latest?.presence == 1) Success else LiveMuted
                    )
                    DarkChip(
                        text = if (latest?.motion == 1) "体动" else "静止",
                        color = if (latest?.motion == 1) Warning else PrimaryBlueSoft
                    )
                }
            }

            if (stale || !running) {
                Text(
                    text = if (stale) "数据中断，正在重连…" else "采集未运行",
                    style = MaterialTheme.typography.labelSmall.copy(color = Warning),
                    modifier = Modifier
                        .align(Alignment.TopEnd)
                        .padding(14.dp)
                )
            }
        }
    }
}

private fun androidx.compose.ui.graphics.drawscope.DrawScope.drawSparkline(
    values: List<Float>,
    color: Color,
    left: Float,
    right: Float,
    bottom: Float,
    height: Float
) {
    if (values.size < 2) {
        drawLine(
            color = Hairline,
            start = Offset(left, bottom),
            end = Offset(right, bottom),
            strokeWidth = 1.dp.toPx()
        )
        return
    }
    val minValue = values.min()
    val maxValue = values.max()
    val range = (maxValue - minValue).coerceAtLeast(1f)
    val stepX = (right - left) / (values.size - 1)
    val path = Path()
    values.forEachIndexed { index, value ->
        val x = left + index * stepX
        val y = bottom - ((value - minValue) / range) * height
        if (index == 0) path.moveTo(x, y) else path.lineTo(x, y)
    }
    val fill = Path().apply {
        addPath(path)
        lineTo(right, bottom)
        lineTo(left, bottom)
        close()
    }
    drawPath(
        path = fill,
        brush = Brush.verticalGradient(
            colors = listOf(color.copy(alpha = 0.18f), Color.Transparent),
            startY = bottom - height,
            endY = bottom
        )
    )
    drawPath(
        path = path,
        color = color.copy(alpha = 0.9f),
        style = Stroke(width = 2.dp.toPx(), cap = StrokeCap.Round)
    )
    val lastX = right
    val lastY = bottom - ((values.last() - minValue) / range) * height
    drawCircle(color = color, radius = 3.dp.toPx(), center = Offset(lastX, lastY))
}

@Composable
private fun LiveMetric(
    label: String,
    value: String,
    unit: String,
    accent: Color,
    modifier: Modifier = Modifier
) {
    Card(
        modifier = modifier,
        shape = RoundedCornerShape(16.dp),
        colors = CardDefaults.cardColors(containerColor = LivePanel),
        border = BorderStroke(1.dp, LiveLine),
        elevation = CardDefaults.cardElevation(defaultElevation = 0.dp)
    ) {
        Column(modifier = Modifier.padding(16.dp)) {
            Text(
                text = label,
                style = MaterialTheme.typography.labelSmall.copy(color = LiveMuted)
            )
            Spacer(modifier = Modifier.height(10.dp))
            Row(verticalAlignment = Alignment.Bottom) {
                Text(
                    text = value,
                    style = MaterialTheme.typography.headlineMedium.copy(
                        color = LiveText,
                        fontWeight = FontWeight.Bold
                    )
                )
                Text(
                    text = unit,
                    style = MaterialTheme.typography.labelSmall.copy(color = LiveMuted),
                    modifier = Modifier.padding(start = 4.dp, bottom = 3.dp)
                )
            }
            Spacer(modifier = Modifier.height(8.dp))
            Box(
                modifier = Modifier
                    .size(6.dp)
                    .clip(CircleShape)
                    .background(accent)
            )
        }
    }
}

@Composable
private fun DarkChip(text: String, color: Color) {
    Text(
        text = text,
        style = MaterialTheme.typography.labelSmall.copy(
            color = color,
            fontWeight = FontWeight.Medium
        ),
        modifier = Modifier
            .clip(RoundedCornerShape(30.dp))
            .background(color.copy(alpha = 0.12f))
            .padding(horizontal = 12.dp, vertical = 5.dp)
    )
}

@Composable
private fun HeartTrendCard(history: List<Float>, stale: Boolean) {
    Card(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(16.dp),
        colors = CardDefaults.cardColors(containerColor = LivePanel),
        border = BorderStroke(1.dp, LiveLine),
        elevation = CardDefaults.cardElevation(defaultElevation = 0.dp)
    ) {
        Column(modifier = Modifier.padding(16.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text(
                    text = "心率趋势",
                    style = MaterialTheme.typography.titleMedium.copy(
                        color = LiveText,
                        fontWeight = FontWeight.SemiBold
                    )
                )
                Spacer(modifier = Modifier.weight(1f))
                Text(
                    text = if (stale) "已暂停" else "最近 ${history.size} 秒",
                    style = MaterialTheme.typography.labelSmall.copy(color = LiveMuted)
                )
            }
            Spacer(modifier = Modifier.height(10.dp))
            Canvas(modifier = Modifier.fillMaxWidth().height(76.dp)) {
                if (history.isEmpty()) {
                    drawLine(
                        color = Hairline,
                        start = Offset(0f, size.height / 2f),
                        end = Offset(size.width, size.height / 2f),
                        strokeWidth = 1.dp.toPx()
                    )
                    return@Canvas
                }
                val minValue = history.min()
                val maxValue = history.max()
                val range = (maxValue - minValue).coerceAtLeast(1f)
                val stepX = size.width / (history.size - 1).coerceAtLeast(1)
                val path = Path()
                history.forEachIndexed { index, value ->
                    val x = index * stepX
                    val y = size.height - ((value - minValue) / range) * size.height
                    if (index == 0) path.moveTo(x, y) else path.lineTo(x, y)
                }
                val fill = Path().apply {
                    addPath(path)
                    lineTo(size.width, size.height)
                    lineTo(0f, size.height)
                    close()
                }
                drawPath(
                    path = fill,
                    brush = Brush.verticalGradient(
                        listOf(PrimaryBlue.copy(alpha = 0.25f), Color.Transparent),
                        startY = 0f,
                        endY = size.height
                    )
                )
                drawPath(
                    path = path,
                    color = PrimaryBlueSoft,
                    style = Stroke(width = 2.dp.toPx(), cap = StrokeCap.Round)
                )
                drawCircle(
                    color = PrimaryBlueSoft,
                    radius = 3.5.dp.toPx(),
                    center = Offset(
                        size.width,
                        size.height - ((history.last() - minValue) / range) * size.height
                    )
                )
            }
        }
    }
}

@Composable
private fun DarkLoading(modifier: Modifier = Modifier) {
    Column(
        modifier = modifier,
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center
    ) {
        CircularProgressIndicator(color = PrimaryBlue, trackColor = LiveLine)
        Spacer(modifier = Modifier.height(14.dp))
        Text("正在连接雷达…", color = LiveMuted)
    }
}

@Composable
private fun DarkError(message: String, onRetry: () -> Unit, modifier: Modifier = Modifier) {
    Column(
        modifier = modifier,
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center
    ) {
        Text(message, color = ErrorRed, style = MaterialTheme.typography.bodyLarge)
        Spacer(modifier = Modifier.height(12.dp))
        TextButton(onClick = onRetry) {
            Text("重新连接", color = PrimaryBlueSoft)
        }
    }
}
