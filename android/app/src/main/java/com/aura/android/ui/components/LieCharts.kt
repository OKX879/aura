package com.aura.android.ui.components

import androidx.compose.animation.core.Animatable
import androidx.compose.animation.core.FastOutSlowInEasing
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.text.AnnotatedString
import androidx.compose.ui.text.drawText
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.rememberTextMeasurer
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aura.android.ui.theme.Cat4
import com.aura.android.ui.theme.ChartData
import com.aura.android.ui.theme.ChartGrid
import com.aura.android.ui.theme.ChartHero
import com.aura.android.ui.theme.ChartTrack
import com.aura.android.ui.theme.IOSLabel
import com.aura.android.ui.theme.IOSSecondaryLabel
import com.aura.android.ui.theme.Paper
import com.aura.android.ui.theme.Ramp6
import kotlinx.coroutines.delay
import kotlin.math.max
import kotlin.math.min

// 图表动画参数
private const val QUART_OUT_MS = 900
private const val DOT_STAGGER_MS = 12
private const val DOT_DURATION_MS = 380
private const val BAR_STAGGER_MS = 95
private const val BAR_DURATION_MS = 620

// 通用卡片容器
@Composable
fun LieCard(
    modifier: Modifier = Modifier,
    content: @Composable ColumnScope.() -> Unit
) {
    Column(
        modifier = modifier
            .clip(RoundedCornerShape(24.dp))
            .background(Paper)
            .padding(horizontal = 20.dp, vertical = 18.dp),
        content = content
    )
}

// 入场动画容器
@Composable
fun LieReveal(
    index: Int = 0,
    content: @Composable () -> Unit
) {
    var shown by remember { mutableStateOf(false) }
    LaunchedEffect(Unit) {
        delay(index * 70L)
        shown = true
    }
    val alpha by animateFloatAsState(
        targetValue = if (shown) 1f else 0f,
        animationSpec = tween(340, easing = FastOutSlowInEasing),
        label = "reveal-alpha"
    )
    val lift by animateFloatAsState(
        targetValue = if (shown) 0f else 14f,
        animationSpec = tween(340, easing = FastOutSlowInEasing),
        label = "reveal-lift"
    )
    Box(
        modifier = Modifier
            .fillMaxWidth()
            .graphicsLayer {
                this.alpha = alpha
                translationY = lift.dp.toPx()
            }
    ) {
        content()
    }
}

// 注记小标题
@Composable
fun LieEyebrow(text: String, modifier: Modifier = Modifier, color: Color = IOSSecondaryLabel) {
    Text(
        text = text,
        style = MaterialTheme.typography.labelSmall,
        color = color,
        modifier = modifier
    )
}

// 发丝折线
@Composable
fun LieHairline(
    values: List<Float>,
    modifier: Modifier = Modifier,
    color: Color = ChartData,
    filled: Boolean = false,
    showLastBead: Boolean = true,
    animationKey: Any? = null,
    animate: Boolean = true
) {
    val clean = values.filter { it.isFinite() }
    if (clean.size < 2) {
        Box(modifier)
        return
    }
    val progress = remember { Animatable(if (animate) 0f else 1f) }
    LaunchedEffect(animationKey) {
        if (!animate) {
            progress.snapTo(1f)
            return@LaunchedEffect
        }
        progress.snapTo(0f)
        progress.animateTo(1f, tween(QUART_OUT_MS, easing = FastOutSlowInEasing))
    }

    val lo = clean.min()
    val hi = clean.max()
    val span = if (hi - lo < 1e-6f) 1f else hi - lo

    Canvas(modifier = modifier) {
        val w = size.width
        val h = size.height
        val stroke = 1.2.dp.toPx()
        val inset = stroke * 2f
        val usableW = w - inset * 2
        val usableH = h - inset * 2
        val stepX = usableW / (clean.size - 1).toFloat()

        fun pointAt(i: Int): Offset {
            val x = inset + stepX * i
            val norm = (clean[i] - lo) / span
            val y = inset + usableH * (1f - norm)
            return Offset(x, y)
        }

        drawLine(
            color = ChartGrid,
            start = Offset(0f, h - inset),
            end = Offset(w, h - inset),
            strokeWidth = 0.8.dp.toPx()
        )

        val drawn = max(2, (clean.size * progress.value).toInt())
        val linePath = Path()
        linePath.moveTo(pointAt(0).x, pointAt(0).y)
        for (i in 1 until drawn) {
            val p = pointAt(i)
            linePath.lineTo(p.x, p.y)
        }

        if (filled) {
            val areaPath = Path().apply {
                addPath(linePath)
                lineTo(pointAt(drawn - 1).x, h - inset)
                lineTo(pointAt(0).x, h - inset)
                close()
            }
            drawPath(path = areaPath, color = color.copy(alpha = 0.14f))
        }

        drawPath(
            path = linePath,
            color = color,
            style = Stroke(width = stroke, cap = StrokeCap.Round)
        )

        if (showLastBead && drawn >= 2) {
            val last = pointAt(drawn - 1)
            drawCircle(color = Paper, radius = stroke * 2.6f, center = last)
            drawCircle(color = color, radius = stroke * 1.5f, center = last)
        }
    }
}

// 胶囊柱状图
@Composable
fun LieBars(
    values: List<Float>,
    modifier: Modifier = Modifier,
    colors: List<Color> = Cat4,
    highlightIndex: Int = -1,
    animate: Boolean = true
) {
    if (values.isEmpty()) {
        Box(modifier)
        return
    }
    val totalMs = (values.size - 1) * BAR_STAGGER_MS + BAR_DURATION_MS
    val progress = remember { Animatable(if (animate) 0f else 1f) }
    // 仅首次播放生长动画
    LaunchedEffect(Unit) {
        if (!animate) {
            progress.snapTo(1f)
            return@LaunchedEffect
        }
        progress.snapTo(0f)
        progress.animateTo(1f, tween(totalMs, easing = FastOutSlowInEasing))
    }
    val hi = values.max().coerceAtLeast(1e-6f)

    Canvas(modifier = modifier) {
        val elapsed = progress.value * totalMs
        val n = values.size
        val gap = size.width * 0.035f
        val barW = (size.width - gap * (n - 1)) / n
        values.forEachIndexed { i, v ->
            val local = ((elapsed - i * BAR_STAGGER_MS) / BAR_DURATION_MS)
                .coerceIn(0f, 1f)
            if (local <= 0f) return@forEachIndexed
            val ratio = (v / hi).coerceIn(0f, 1f) * local
            val barH = size.height * ratio
            val left = i * (barW + gap)
            val color = if (i == highlightIndex) ChartHero
            else colors.getOrElse(i % colors.size) { ChartData }
            drawRoundRect(
                color = color,
                topLeft = Offset(left, size.height - barH),
                size = Size(barW, barH),
                cornerRadius = CornerRadius(barW * 0.18f, barW * 0.18f)
            )
        }
    }
}

// 单值发丝环
@Composable
fun LieGauge(
    fraction: Float,
    modifier: Modifier = Modifier,
    color: Color = ChartData,
    trackColor: Color = ChartTrack,
    strokeWidth: androidx.compose.ui.unit.Dp = 2.6.dp,
    animate: Boolean = true
) {
    val animated = remember { Animatable(if (animate) 0f else fraction.coerceIn(0f, 1f)) }
    LaunchedEffect(fraction) {
        if (!animate) {
            animated.snapTo(fraction.coerceIn(0f, 1f))
            return@LaunchedEffect
        }
        animated.animateTo(
            fraction.coerceIn(0f, 1f),
            tween(QUART_OUT_MS, easing = FastOutSlowInEasing)
        )
    }
    Canvas(modifier = modifier) {
        val stroke = strokeWidth.toPx()
        val radius = (min(size.width, size.height) - stroke) / 2f
        val topLeft = Offset((size.width - radius * 2) / 2f, (size.height - radius * 2) / 2f)
        val arcSize = Size(radius * 2, radius * 2)
        drawArc(
            color = trackColor,
            startAngle = -90f,
            sweepAngle = 360f,
            useCenter = false,
            topLeft = topLeft,
            size = arcSize,
            style = Stroke(width = stroke * 0.7f, cap = StrokeCap.Round)
        )
        drawArc(
            color = color,
            startAngle = -90f,
            sweepAngle = 360f * animated.value,
            useCenter = false,
            topLeft = topLeft,
            size = arcSize,
            style = Stroke(width = stroke, cap = StrokeCap.Round)
        )
        // 末端圆点
        if (animated.value > 0.02f) {
            val angle = Math.toRadians((-90f + 360f * animated.value).toDouble())
            val cx = topLeft.x + radius + radius * Math.cos(angle).toFloat()
            val cy = topLeft.y + radius + radius * Math.sin(angle).toFloat()
            drawCircle(color = color, radius = stroke * 1.1f, center = Offset(cx, cy))
        }
    }
}

// 点阵热力图
@Composable
fun LieDotHeat(
    values: List<Float>,
    modifier: Modifier = Modifier,
    columns: Int = 12,
    animate: Boolean = true
) {
    if (values.isEmpty()) {
        Box(modifier)
        return
    }
    val lo = values.min()
    val hi = values.max()
    val span = if (hi - lo < 1e-6f) 1f else hi - lo
    val rows = (values.size + columns - 1) / columns
    val totalMs = (values.size - 1) * DOT_STAGGER_MS + DOT_DURATION_MS
    val progress = remember { Animatable(if (animate) 0f else 1f) }
    LaunchedEffect(values.size) {
        if (!animate) {
            progress.snapTo(1f)
            return@LaunchedEffect
        }
        progress.snapTo(0f)
        progress.animateTo(1f, tween(totalMs, easing = FastOutSlowInEasing))
    }

    Canvas(modifier = modifier) {
        val elapsed = progress.value * totalMs
        val gap = 4.dp.toPx()
        val cell = min(
            (size.width - gap * (columns - 1)) / columns,
            (size.height - gap * (rows - 1)) / max(1, rows)
        )
        values.forEachIndexed { index, v ->
            val local = ((elapsed - index * DOT_STAGGER_MS) / DOT_DURATION_MS)
                .coerceIn(0f, 1f)
            if (local <= 0f) return@forEachIndexed
            val norm = ((v - lo) / span).coerceIn(0f, 1f)
            val idx = (norm * (Ramp6.size - 1)).toInt()
            val col = index % columns
            val row = index / columns
            val center = Offset(
                col * (cell + gap) + cell / 2f,
                row * (cell + gap) + cell / 2f
            )
            drawCircle(
                color = Ramp6[idx].copy(alpha = local),
                radius = cell / 2f * local,
                center = center
            )
        }
    }
}

// 数字滚动动画
@Composable
fun AnimatedCounter(
    value: Float?,
    modifier: Modifier = Modifier,
    style: androidx.compose.ui.text.TextStyle = MaterialTheme.typography.displayLarge,
    color: Color = IOSLabel,
    formatter: (Float) -> String = { it.toInt().toString() }
) {
    val target = value ?: 0f
    val animated = remember { Animatable(if (value == null) 0f else 0f) }
    LaunchedEffect(target) {
        if (value == null) {
            animated.snapTo(0f)
        } else {
            animated.animateTo(target, tween(QUART_OUT_MS, easing = FastOutSlowInEasing))
        }
    }
    Text(
        text = if (value == null) "--" else formatter(animated.value),
        style = style,
        color = color,
        modifier = modifier
    )
}

// 指标行
@Composable
fun LieMetricRow(
    label: String,
    value: String,
    unit: String,
    modifier: Modifier = Modifier,
    valueColor: Color = IOSLabel,
    trailing: (@Composable () -> Unit)? = null
) {
    Row(
        modifier = modifier.fillMaxWidth(),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.SpaceBetween
    ) {
        Text(
            text = label,
            style = MaterialTheme.typography.labelMedium,
            color = IOSSecondaryLabel
        )
        Row(verticalAlignment = Alignment.Bottom) {
            Text(
                text = value,
                style = MaterialTheme.typography.displayMedium,
                color = valueColor
            )
            Spacer(Modifier.width(4.dp))
            Text(
                text = unit,
                style = MaterialTheme.typography.labelMedium,
                color = IOSSecondaryLabel,
                modifier = Modifier.padding(bottom = 6.dp)
            )
            if (trailing != null) {
                Spacer(Modifier.width(10.dp))
                trailing()
            }
        }
    }
}

// 状态点文字
@Composable
fun LieStatusDot(
    color: Color,
    text: String,
    modifier: Modifier = Modifier,
    pulse: Boolean = false
) {
    val pulseAlpha = remember { Animatable(1f) }
    LaunchedEffect(pulse) {
        if (!pulse) return@LaunchedEffect
        while (true) {
            pulseAlpha.animateTo(0.35f, tween(700, easing = FastOutSlowInEasing))
            pulseAlpha.animateTo(1f, tween(700, easing = FastOutSlowInEasing))
        }
    }
    Row(modifier = modifier, verticalAlignment = Alignment.CenterVertically) {
        Box(
            modifier = Modifier
                .size(7.dp)
                .clip(CircleShape)
                .background(color.copy(alpha = pulseAlpha.value))
        )
        Spacer(Modifier.width(7.dp))
        Text(
            text = text,
            style = MaterialTheme.typography.labelMedium,
            fontWeight = FontWeight.Medium,
            color = IOSSecondaryLabel
        )
    }
}

// 细分隔线
@Composable
fun LieRule(modifier: Modifier = Modifier) {
    Box(
        modifier = modifier
            .fillMaxWidth()
            .height(1.dp)
            .background(ChartGrid.copy(alpha = 0.6f))
    )
}

// 带坐标轴的折线图
@Composable
fun LieTrendChart(
    values: List<Float>,
    modifier: Modifier = Modifier,
    color: Color = ChartData,
    filled: Boolean = true,
    yMin: Float = 0f,
    yMax: Float = 100f,
    yTicks: List<Float> = listOf(0f, 50f, 100f),
    xLabels: List<String> = listOf("-48s", "-32s", "-16s", "现在"),
    animate: Boolean = true
) {
    val clean = values.filter { it.isFinite() }
    if (clean.size < 2) {
        Box(modifier)
        return
    }
    val measurer = rememberTextMeasurer()
    val labelStyle = MaterialTheme.typography.labelSmall.copy(
        fontSize = 9.sp,
        letterSpacing = 0.3.sp
    )
    val progress = remember { Animatable(if (animate) 0f else 1f) }
    // 仅首次播放绘制动画
    LaunchedEffect(Unit) {
        if (!animate) {
            progress.snapTo(1f)
            return@LaunchedEffect
        }
        progress.snapTo(0f)
        progress.animateTo(1f, tween(QUART_OUT_MS, easing = FastOutSlowInEasing))
    }

    Canvas(modifier = modifier) {
        val axisW = 24.dp.toPx()
        val axisH = 16.dp.toPx()
        val plotW = size.width - axisW
        val plotH = size.height - axisH
        val span = (yMax - yMin).coerceAtLeast(1e-6f)
        fun yOf(v: Float): Float = plotH * (1f - ((v - yMin) / span).coerceIn(0f, 1f))

        // 网格 + Y 刻度
        yTicks.forEach { tick ->
            val y = yOf(tick)
            drawLine(
                color = ChartGrid.copy(alpha = 0.55f),
                start = Offset(axisW, y),
                end = Offset(size.width, y),
                strokeWidth = 0.7.dp.toPx()
            )
            val layout = measurer.measure(
                AnnotatedString(tick.toInt().toString()),
                style = labelStyle
            )
            drawText(
                textLayoutResult = layout,
                color = IOSSecondaryLabel.copy(alpha = 0.75f),
                topLeft = Offset(0f, (y - layout.size.height / 2f).coerceAtLeast(0f))
            )
        }

        // X 轴时间线
        drawLine(
            color = ChartGrid,
            start = Offset(axisW, plotH),
            end = Offset(size.width, plotH),
            strokeWidth = 0.8.dp.toPx()
        )
        xLabels.forEachIndexed { i, label ->
            if (xLabels.size < 2) return@forEachIndexed
            val x = axisW + plotW * i / (xLabels.size - 1).toFloat()
            val layout = measurer.measure(AnnotatedString(label), style = labelStyle)
            val left = (x - layout.size.width / 2f)
                .coerceIn(axisW, size.width - layout.size.width)
            drawText(
                textLayoutResult = layout,
                color = IOSSecondaryLabel.copy(alpha = 0.75f),
                topLeft = Offset(left, plotH + 3.dp.toPx())
            )
        }

        // 曲线
        val stroke = 1.2.dp.toPx()
        val stepX = plotW / (clean.size - 1).toFloat()
        fun pointAt(i: Int) = Offset(axisW + stepX * i, yOf(clean[i]))
        val drawn = max(2, (clean.size * progress.value).toInt())
        val path = Path().apply {
            moveTo(pointAt(0).x, pointAt(0).y)
            for (i in 1 until drawn) {
                val p = pointAt(i)
                lineTo(p.x, p.y)
            }
        }
        if (filled) {
            val area = Path().apply {
                addPath(path)
                lineTo(pointAt(drawn - 1).x, plotH)
                lineTo(pointAt(0).x, plotH)
                close()
            }
            drawPath(area, color.copy(alpha = 0.13f))
        }
        drawPath(path, color, style = Stroke(width = stroke, cap = StrokeCap.Round))
        if (drawn >= 2) {
            val last = pointAt(drawn - 1)
            drawCircle(Paper, radius = stroke * 2.4f, center = last)
            drawCircle(color, radius = stroke * 1.4f, center = last)
        }
    }
}

// 带坐标轴的柱状图
@Composable
fun LieBarChart(
    values: List<Float>,
    modifier: Modifier = Modifier,
    color: Color = ChartData,
    highlightLast: Boolean = true,
    splitIndex: Int = -1,
    yMax: Float = 100f,
    yTicks: List<Float> = listOf(0f, 50f, 100f),
    xLabels: List<String> = listOf("-48s", "-32s", "-16s", "现在"),
    animate: Boolean = true
) {
    if (values.isEmpty()) {
        Box(modifier)
        return
    }
    val measurer = rememberTextMeasurer()
    val labelStyle = MaterialTheme.typography.labelSmall.copy(
        fontSize = 9.sp,
        letterSpacing = 0.3.sp
    )
    val totalMs = (values.size - 1) * BAR_STAGGER_MS + BAR_DURATION_MS
    val progress = remember { Animatable(if (animate) 0f else 1f) }
    // 仅首次播放生长动画
    LaunchedEffect(Unit) {
        if (!animate) {
            progress.snapTo(1f)
            return@LaunchedEffect
        }
        progress.snapTo(0f)
        progress.animateTo(1f, tween(totalMs, easing = FastOutSlowInEasing))
    }

    Canvas(modifier = modifier) {
        val axisW = 24.dp.toPx()
        val axisH = 16.dp.toPx()
        val plotW = size.width - axisW
        val plotH = size.height - axisH
        val scale = yMax.coerceAtLeast(1e-6f)

        yTicks.forEach { tick ->
            val y = plotH * (1f - (tick / scale).coerceIn(0f, 1f))
            drawLine(
                color = ChartGrid.copy(alpha = 0.55f),
                start = Offset(axisW, y),
                end = Offset(size.width, y),
                strokeWidth = 0.7.dp.toPx()
            )
            val layout = measurer.measure(
                AnnotatedString(tick.toInt().toString()),
                style = labelStyle
            )
            drawText(
                textLayoutResult = layout,
                color = IOSSecondaryLabel.copy(alpha = 0.75f),
                topLeft = Offset(0f, (y - layout.size.height / 2f).coerceAtLeast(0f))
            )
        }

        drawLine(
            color = ChartGrid,
            start = Offset(axisW, plotH),
            end = Offset(size.width, plotH),
            strokeWidth = 0.8.dp.toPx()
        )
        xLabels.forEachIndexed { i, label ->
            if (xLabels.size < 2) return@forEachIndexed
            val x = axisW + plotW * i / (xLabels.size - 1).toFloat()
            val layout = measurer.measure(AnnotatedString(label), style = labelStyle)
            val left = (x - layout.size.width / 2f)
                .coerceIn(axisW, size.width - layout.size.width)
            drawText(
                textLayoutResult = layout,
                color = IOSSecondaryLabel.copy(alpha = 0.75f),
                topLeft = Offset(left, plotH + 3.dp.toPx())
            )
        }

        val elapsed = progress.value * totalMs
        val n = values.size
        val gapRatio = if (n > 24) 0.18f else 0.32f
        // 按固定槽位排版
        val slots = maxOf(n, 16)
        val slot = plotW / slots
        val barW = slot * (1f - gapRatio)
        values.forEachIndexed { i, v ->
            val local = ((elapsed - i * BAR_STAGGER_MS) / BAR_DURATION_MS).coerceIn(0f, 1f)
            if (local <= 0f) return@forEachIndexed
            val ratio = (v / scale).coerceIn(0f, 1f) * local
            val barH = plotH * ratio
            val left = axisW + i * slot + (slot - barW) / 2f
            val isLast = i == n - 1
            val inFirstHalf = splitIndex in 1 until n && i < splitIndex
            val barColor = when {
                highlightLast && isLast -> ChartHero
                inFirstHalf -> color.copy(alpha = 0.40f)
                else -> color
            }
            drawRoundRect(
                color = barColor.copy(alpha = if (isLast) 1f else 0.82f),
                topLeft = Offset(left, plotH - barH),
                size = Size(barW, barH),
                cornerRadius = CornerRadius(barW * 0.45f, barW * 0.45f)
            )
        }
    }
}
