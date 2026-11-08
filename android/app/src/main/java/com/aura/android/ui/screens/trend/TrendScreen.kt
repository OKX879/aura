package com.aura.android.ui.screens.trend

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ArrowBack
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aura.android.ui.components.LieBarChart
import com.aura.android.ui.components.LieCard
import com.aura.android.ui.components.LieEyebrow
import com.aura.android.ui.components.LieReveal
import com.aura.android.ui.components.LieTrendChart
import com.aura.android.ui.theme.ChartData
import com.aura.android.ui.theme.ChartHero
import com.aura.android.ui.theme.Hint
import com.aura.android.ui.theme.OnSurface
import com.aura.android.ui.theme.Surface

// 趋势子页，展示本周、本月与对比
@Composable
fun TrendScreen(onBack: () -> Unit) {
    var tab by remember { mutableIntStateOf(0) }
    val tabs = listOf("本周", "本月", "对比")

    Scaffold(
        containerColor = androidx.compose.ui.graphics.Color.Transparent,
        topBar = {
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .background(Surface.copy(alpha = 0.96f))
                    .statusBarsPadding()
                    .padding(start = 8.dp, end = 20.dp, top = 6.dp, bottom = 12.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                IconButton(onClick = onBack) {
                    Icon(Icons.Default.ArrowBack, contentDescription = "返回", tint = OnSurface)
                }
                Column {
                    LieEyebrow("TRENDS · ${TrendData.YEAR_LABEL}")
                    Spacer(modifier = Modifier.height(4.dp))
                    Text(
                        text = "趋势",
                        style = MaterialTheme.typography.displayLarge.copy(
                            color = OnSurface,
                            fontSize = 36.sp
                        )
                    )
                }
            }
        }
    ) { padding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .verticalScroll(rememberScrollState())
                .padding(padding)
                .padding(horizontal = 20.dp),
            verticalArrangement = Arrangement.spacedBy(14.dp)
        ) {
            // 分段选择器
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(22.dp)
            ) {
                tabs.forEachIndexed { index, label ->
                    val selected = tab == index
                    Column(
                        modifier = Modifier
                            .clickable { tab = index }
                            .padding(vertical = 4.dp),
                        horizontalAlignment = Alignment.CenterHorizontally
                    ) {
                        Text(
                            text = label,
                            style = MaterialTheme.typography.labelMedium.copy(
                                color = if (selected) OnSurface else Hint,
                                fontWeight = if (selected) FontWeight.Bold else FontWeight.Normal
                            )
                        )
                        Spacer(modifier = Modifier.height(5.dp))
                        Box(
                            modifier = Modifier
                                .width(if (selected) 20.dp else 0.dp)
                                .height(2.dp)
                                .background(if (selected) ChartHero else Color.Transparent)
                        )
                    }
                }
            }

            when (tab) {
                0 -> LieReveal(0) { WeekCard() }
                1 -> LieReveal(0) { MonthCard() }
                else -> LieReveal(0) { CompareCard() }
            }

            Spacer(modifier = Modifier.height(112.dp))
        }
    }
}

@Composable
private fun WeekCard() {
    val values = TrendData.week
    LieCard {
        LieEyebrow("专注度 / FOCUS · 本周日均")
        Spacer(modifier = Modifier.height(10.dp))
        Text(
            text = TrendData.weekAverage().toInt().toString(),
            style = MaterialTheme.typography.displayLarge.copy(
                color = OnSurface,
                fontSize = 52.sp
            )
        )
        Text(
            text = "本周均值",
            style = MaterialTheme.typography.labelSmall.copy(color = Hint)
        )
        Spacer(modifier = Modifier.height(14.dp))
        LieTrendChart(
            values = values,
            modifier = Modifier
                .fillMaxWidth()
                .height(168.dp),
            color = ChartData,
            yMin = 40f,
            yMax = 100f,
            yTicks = listOf(40f, 70f, 100f),
            xLabels = TrendData.weekLabels
        )
        Spacer(modifier = Modifier.height(14.dp))
        StatRow(
            listOf(
                "最高" to values.max().toInt().toString(),
                "最低" to values.min().toInt().toString(),
                "波动" to (values.max() - values.min()).toInt().toString()
            )
        )
    }
}

@Composable
private fun MonthCard() {
    val values = TrendData.month
    LieCard {
        LieEyebrow("专注度 / FOCUS · 本月日均")
        Spacer(modifier = Modifier.height(10.dp))
        Text(
            text = TrendData.monthAverage().toInt().toString(),
            style = MaterialTheme.typography.displayLarge.copy(
                color = OnSurface,
                fontSize = 52.sp
            )
        )
        Text(
            text = "本月均值",
            style = MaterialTheme.typography.labelSmall.copy(color = Hint)
        )
        Spacer(modifier = Modifier.height(14.dp))
        LieTrendChart(
            values = values,
            modifier = Modifier
                .fillMaxWidth()
                .height(168.dp),
            color = ChartData,
            yMin = 40f,
            yMax = 100f,
            yTicks = listOf(40f, 70f, 100f),
            xLabels = TrendData.monthLabels
        )
        Spacer(modifier = Modifier.height(14.dp))
        StatRow(
            listOf(
                "最高" to values.max().toInt().toString(),
                "最低" to values.min().toInt().toString(),
                "达标天数" to values.count { it >= 70f }.toString()
            )
        )
    }
}

@Composable
private fun CompareCard() {
    val merged = TrendData.lastWeek + TrendData.week
    val delta = TrendData.delta()
    LieCard {
        LieEyebrow("本周 vs 上周 / WEEK OVER WEEK")
        Spacer(modifier = Modifier.height(10.dp))
        Row(verticalAlignment = Alignment.Bottom) {
            Text(
                text = if (delta >= 0) "+${"%.1f".format(delta)}" else "%.1f".format(delta),
                style = MaterialTheme.typography.displayLarge.copy(
                    color = if (delta >= 0) ChartData else Color(0xFFB4472B),
                    fontSize = 48.sp
                )
            )
            Text(
                text = "分 较上周",
                style = MaterialTheme.typography.labelMedium.copy(color = Hint),
                modifier = Modifier.padding(start = 6.dp, bottom = 8.dp)
            )
        }
        Spacer(modifier = Modifier.height(12.dp))
        LieBarChart(
            values = merged,
            modifier = Modifier
                .fillMaxWidth()
                .height(160.dp),
            color = ChartData,
            highlightLast = false,
            splitIndex = TrendData.lastWeek.size,
            yMax = 100f,
            yTicks = listOf(0f, 50f, 100f),
            xLabels = listOf("上周一", "上周四", "本周一", "本周日")
        )
        Spacer(modifier = Modifier.height(12.dp))
        Row(verticalAlignment = Alignment.CenterVertically) {
            LegendDot(ChartData.copy(alpha = 0.40f), "上周")
            Spacer(modifier = Modifier.width(16.dp))
            LegendDot(ChartData, "本周")
        }
        Spacer(modifier = Modifier.height(14.dp))
        StatRow(
            listOf(
                "上周均值" to TrendData.lastWeekAverage().toInt().toString(),
                "本周均值" to TrendData.weekAverage().toInt().toString(),
                "波动差" to TrendData.rms().toInt().toString()
            )
        )
    }
}

@Composable
private fun LegendDot(color: Color, label: String) {
    Row(verticalAlignment = Alignment.CenterVertically) {
        Box(
            modifier = Modifier
                .width(8.dp)
                .height(8.dp)
                .clip(CircleShape)
                .background(color)
        )
        Spacer(modifier = Modifier.width(6.dp))
        Text(
            text = label,
            style = MaterialTheme.typography.labelSmall.copy(color = Hint)
        )
    }
}

@Composable
private fun StatRow(items: List<Pair<String, String>>) {
    Row(modifier = Modifier.fillMaxWidth()) {
        items.forEach { (label, value) ->
            Column(modifier = Modifier.weight(1f)) {
                Text(
                    text = value,
                    style = MaterialTheme.typography.headlineMedium.copy(
                        color = OnSurface,
                        fontSize = 20.sp
                    )
                )
                Spacer(modifier = Modifier.height(2.dp))
                Text(
                    text = label,
                    style = MaterialTheme.typography.labelSmall.copy(color = Hint)
                )
            }
        }
    }
}

// 趋势入口提示
@Composable
fun TrendEntryHint(modifier: Modifier = Modifier) {
    Box(
        modifier = modifier
            .clip(RoundedCornerShape(10.dp))
            .background(ChartHero.copy(alpha = 0.14f))
            .padding(horizontal = 10.dp, vertical = 5.dp)
    ) {
        Text(
            text = "查看趋势 ›",
            style = MaterialTheme.typography.labelSmall.copy(color = ChartHero)
        )
    }
}
