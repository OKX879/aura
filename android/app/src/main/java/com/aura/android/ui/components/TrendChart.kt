package com.aura.android.ui.components

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.unit.dp
import com.aura.android.data.model.TrendPoint
import com.aura.android.ui.theme.AppleBlue
import com.aura.android.ui.theme.Paper

// 简单折线图
@Composable
fun TrendChart(
    points: List<TrendPoint>,
    modifier: Modifier = Modifier,
    lineColor: Color = AppleBlue
) {
    if (points.isEmpty()) return

    Canvas(
        modifier = modifier
            .fillMaxWidth()
            .height(140.dp)
    ) {
        val width = size.width
        val height = size.height
        val padding = 8.dp.toPx()
        val values = points.map { it.averageFocus.toFloat() }
        val min = values.minOrNull() ?: 0f
        val max = values.maxOrNull() ?: 100f
        val range = (max - min).coerceAtLeast(1f)

        val stepX = (width - 2 * padding) / (points.size - 1).coerceAtLeast(1)

        val path = Path()
        points.forEachIndexed { index, point ->
            val x = padding + index * stepX
            val normalized = (point.averageFocus.toFloat() - min) / range
            val y = height - padding - normalized * (height - 2 * padding)
            if (index == 0) path.moveTo(x, y) else path.lineTo(x, y)
        }

        // 填充渐变区域
        val fillPath = Path().apply {
            addPath(path)
            lineTo(width - padding, height - padding)
            lineTo(padding, height - padding)
            close()
        }
        drawPath(
            path = fillPath,
            brush = Brush.verticalGradient(
                colors = listOf(lineColor.copy(alpha = 0.2f), Color.Transparent),
                startY = padding,
                endY = height - padding
            )
        )

        // 折线
        drawPath(
            path = path,
            color = lineColor,
            style = Stroke(width = 3.dp.toPx())
        )

        // 数据点
        points.forEachIndexed { index, point ->
            val x = padding + index * stepX
            val normalized = (point.averageFocus.toFloat() - min) / range
            val y = height - padding - normalized * (height - 2 * padding)
            drawCircle(
                color = Paper,
                radius = 4.dp.toPx(),
                center = Offset(x, y)
            )
            drawCircle(
                color = lineColor,
                radius = 3.dp.toPx(),
                center = Offset(x, y)
            )
        }
    }
}
