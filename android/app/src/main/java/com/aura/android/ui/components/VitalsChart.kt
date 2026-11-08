package com.aura.android.ui.components

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.unit.dp
import com.aura.android.data.model.VitalsPoint
import com.aura.android.ui.theme.AppleBlue
import com.aura.android.ui.theme.Paper

// 生命体征折线图
@Composable
fun VitalsChart(
    points: List<VitalsPoint>,
    metric: VitalsMetric,
    modifier: Modifier = Modifier,
    lineColor: Color = AppleBlue
) {
    if (points.isEmpty()) return

    val values = points.mapNotNull { p ->
        when (metric) {
            VitalsMetric.HEART_RATE -> p.heartRate
            VitalsMetric.RESPIRATION -> p.respirationRate
            VitalsMetric.STRESS -> p.stressLevel
            VitalsMetric.FATIGUE -> p.fatigueLevel
            VitalsMetric.FOCUS -> p.focusScore
        }?.toFloat()
    }
    if (values.isEmpty()) return

    Canvas(
        modifier = modifier
            .fillMaxWidth()
            .height(120.dp)
    ) {
        val width = size.width
        val height = size.height
        val padding = 8.dp.toPx()
        val min = values.min()
        val max = values.max()
        val range = (max - min).coerceAtLeast(1f)
        val stepX = (width - 2 * padding) / (values.size - 1).coerceAtLeast(1)

        val path = Path()
        values.forEachIndexed { index, value ->
            val x = padding + index * stepX
            val normalized = (value - min) / range
            val y = height - padding - normalized * (height - 2 * padding)
            if (index == 0) path.moveTo(x, y) else path.lineTo(x, y)
        }

        // 渐变填充
        val fillPath = Path().apply {
            addPath(path)
            lineTo(padding + (values.size - 1) * stepX, height - padding)
            lineTo(padding, height - padding)
            close()
        }
        drawPath(
            path = fillPath,
            brush = Brush.verticalGradient(
                colors = listOf(lineColor.copy(alpha = 0.25f), Color.Transparent),
                startY = padding,
                endY = height - padding
            )
        )

        // 折线
        drawPath(
            path = path,
            color = lineColor,
            style = Stroke(width = 2.5.dp.toPx(), cap = StrokeCap.Round)
        )

        // 数据点
        values.forEachIndexed { index, value ->
            val x = padding + index * stepX
            val normalized = (value - min) / range
            val y = height - padding - normalized * (height - 2 * padding)
            drawCircle(color = Paper, radius = 3.5.dp.toPx(), center = Offset(x, y))
            drawCircle(color = lineColor, radius = 2.5.dp.toPx(), center = Offset(x, y))
        }
    }
}

enum class VitalsMetric(val label: String, val unit: String) {
    HEART_RATE("心率", "BPM"),
    RESPIRATION("呼吸", "次/分"),
    STRESS("压力", ""),
    FATIGUE("疲劳", ""),
    FOCUS("专注度", "")
}
