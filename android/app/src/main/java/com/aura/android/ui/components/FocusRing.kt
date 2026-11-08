package com.aura.android.ui.components

import androidx.compose.animation.core.Animatable
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.size
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.aura.android.ui.theme.LabelSecondary
import com.aura.android.ui.theme.PrimaryBlue
import com.aura.android.ui.theme.Success
import com.aura.android.ui.theme.Warning

// 专注度分数环
@Composable
fun FocusRing(
    score: Double,
    modifier: Modifier = Modifier,
    size: androidx.compose.ui.unit.Dp = 200.dp,
    strokeWidth: androidx.compose.ui.unit.Dp = 18.dp
) {
    val coercedScore = score.coerceIn(0.0, 100.0).toFloat()
    val animatedScore = remember { Animatable(0f) }
    LaunchedEffect(score) {
        animatedScore.animateTo(
            targetValue = coercedScore,
            animationSpec = tween(durationMillis = 1200)
        )
    }

    val ringColor = when (coercedScore) {
        in 0f..59f -> Warning
        in 60f..79f -> PrimaryBlue
        else -> Success
    }

    Box(
        modifier = modifier.size(size),
        contentAlignment = Alignment.Center
    ) {
        Canvas(modifier = Modifier.size(size)) {
            val sweep = 360f * (animatedScore.value / 100f)
            drawArc(
                color = Color(0xFFE5E5EA),
                startAngle = -90f,
                sweepAngle = 360f,
                useCenter = false,
                style = Stroke(width = strokeWidth.toPx(), cap = StrokeCap.Round)
            )
            drawArc(
                brush = Brush.sweepGradient(
                    0f to PrimaryBlue.copy(alpha = 0.35f),
                    0.7f to ringColor,
                    1f to ringColor
                ),
                startAngle = -90f,
                sweepAngle = sweep,
                useCenter = false,
                style = Stroke(width = strokeWidth.toPx(), cap = StrokeCap.Round)
            )
        }

        androidx.compose.foundation.layout.Column(horizontalAlignment = Alignment.CenterHorizontally) {
            Text(
                text = "${animatedScore.value.toInt()}",
                style = MaterialTheme.typography.displayLarge.copy(
                    fontWeight = FontWeight.Bold,
                    color = MaterialTheme.colorScheme.onBackground
                )
            )
            Text(
                text = "专注度",
                style = MaterialTheme.typography.bodyMedium.copy(
                    color = LabelSecondary
                )
            )
        }
    }
}
