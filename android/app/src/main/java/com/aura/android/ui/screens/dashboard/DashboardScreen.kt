package com.aura.android.ui.screens.dashboard

import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.tween
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
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aura.android.data.DeviceRepository
import com.aura.android.data.LiveMonitor
import com.aura.android.data.LocalMode
import com.aura.android.data.LocalStore
import com.aura.android.data.api.ApiClient
import com.aura.android.data.api.ApiResult
import com.aura.android.data.model.FocusRecord
import com.aura.android.data.model.MobileDashboard
import com.aura.android.data.model.TrendPoint
import com.aura.android.ui.components.AddDeviceDialog
import com.aura.android.ui.components.AlertItem
import com.aura.android.ui.components.AuraCard
import com.aura.android.ui.components.DeviceItem
import com.aura.android.ui.components.AnimatedCounter
import com.aura.android.ui.components.LieBarChart
import com.aura.android.ui.components.LieCard
import com.aura.android.ui.components.LieEyebrow
import com.aura.android.ui.components.LieGauge
import com.aura.android.ui.components.LieHairline
import com.aura.android.ui.components.LieReveal
import com.aura.android.ui.components.LieRule
import com.aura.android.ui.components.LieStatusDot
import com.aura.android.ui.components.LieTrendChart
import com.aura.android.ui.components.TrendChart
import com.aura.android.ui.components.VitalsChart
import com.aura.android.ui.components.VitalsMetric
import com.aura.android.ui.theme.ChartData
import com.aura.android.ui.theme.ChartHero
import com.aura.android.ui.theme.Hairline
import com.aura.android.ui.theme.Hint
import com.aura.android.ui.theme.IOSGreen
import com.aura.android.ui.theme.PrimaryContainer
import com.aura.android.ui.theme.Success
import com.aura.android.ui.theme.IOSOrange
import com.aura.android.ui.theme.IOSPurple
import com.aura.android.ui.theme.IOSRed
import com.aura.android.ui.theme.Muted
import com.aura.android.ui.theme.OnSurface
import com.aura.android.ui.theme.Paper
import com.aura.android.ui.theme.PrimaryBlue
import com.aura.android.ui.theme.SecondaryContainer
import com.aura.android.ui.theme.Surface
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import kotlinx.coroutines.launch

// 今日首页
@Composable
fun DashboardScreen(
    viewModel: DashboardViewModel,
    onOpenLive: () -> Unit = {},
    onOpenDevices: () -> Unit = {},
    onOpenAlerts: () -> Unit = {},
    onOpenChat: () -> Unit = {},
    onOpenTrend: () -> Unit = {}
) {
    val scrollState = rememberScrollState()
    var showAddDevice by remember { mutableStateOf(false) }
    val s = com.aura.android.ui.i18n.LocalStrings.current
    val liveSnapshot by LiveMonitor.snapshot.collectAsState()
    val liveDerived by LiveMonitor.derived.collectAsState()
    val liveScores by LiveMonitor.scores.collectAsState()
    val liveAlerts by LiveMonitor.alerts.collectAsState()
    val scope = rememberCoroutineScope()

    Scaffold(
        containerColor = androidx.compose.ui.graphics.Color.Transparent,
        topBar = {
            DashboardNav(
                updatedAt = viewModel.lastUpdatedAt,
                onRefresh = { viewModel.loadDashboard() },
                onOpenChat = onOpenChat
            )
        }
    ) { padding ->
        when (val state = viewModel.uiState) {
            is ApiResult.Loading -> {
                DashboardLoading(Modifier.fillMaxSize().padding(padding))
            }
            is ApiResult.Error -> {
                DashboardError(
                    message = state.message,
                    onRetry = { viewModel.loadDashboard() },
                    modifier = Modifier.fillMaxSize().padding(padding)
                )
            }
            is ApiResult.Success -> {
                val dashboard = state.data
                val latest = dashboard.latestRecord
                var appeared by remember { mutableStateOf(false) }
                LaunchedEffect(Unit) { appeared = true }
                // 本地模式读取可变存储
                val onlineCount = if (LocalMode.ENABLED) LocalStore.onlineCount() else dashboard.deviceSummary.online
                val totalDevices = if (LocalMode.ENABLED) LocalStore.devices.size else dashboard.deviceSummary.total
                // 告警以本机实时规则为准
                val unresolved = if (LocalMode.ENABLED) LocalStore.unresolvedAlertCount() else liveAlerts.size
                val recentAlerts = if (LocalMode.ENABLED) LocalStore.recentAlerts() else liveAlerts.take(3)
                val recentDevices = if (LocalMode.ENABLED) LocalStore.recentDevices() else dashboard.deviceSummary.recent
                // 优先展示雷达实时值
                val liveHr = liveDerived.hr?.toDouble()?.takeIf { it > 0 }
                val heroHr = liveHr ?: latest?.heartRate
                val heroStatus = if (liveHr != null) {
                    s.dashboard.liveUpdate
                } else {
                    latest?.recordedAt?.let { "${s.dashboard.lastRecord} ${formatRecordTime(it)}" } ?: s.dashboard.noRecord
                }
                val alpha by animateFloatAsState(
                    targetValue = if (appeared) 1f else 0f,
                    animationSpec = tween(350),
                    label = "dashboardAlpha"
                )
                Column(
                    modifier = Modifier
                        .fillMaxSize()
                        .verticalScroll(scrollState)
                        .padding(padding)
                        .padding(horizontal = 20.dp)
                        .graphicsLayer { this.alpha = alpha },
                    verticalArrangement = Arrangement.spacedBy(12.dp)
                ) {
                    Spacer(modifier = Modifier.height(4.dp))
                    HeroHeartCard(hr = heroHr, statusText = heroStatus, live = liveHr != null)
                    MetricStrip(
                        respiration = liveDerived.resp?.toDouble() ?: latest?.respirationRate,
                        focus = liveScores.focus?.toDouble() ?: latest?.focusScore,
                        stress = liveScores.stress?.toDouble() ?: latest?.stressLevel
                    )

                    SectionTitle(s.dashboard.shortcuts)
                    GroupCard {
                        QuickRow(
                            icon = "实",
                            color = PrimaryBlue,
                            title = s.dashboard.liveMonitor,
                            subtitle = s.dashboard.liveMonitorSubtitle,
                            onClick = onOpenLive
                        )
                        RowDivider()
                        QuickRow(
                            icon = "设",
                            color = IOSGreen,
                            title = "设备",
                            subtitle = "$onlineCount / $totalDevices ${s.dashboard.devicesSubtitle}",
                            onClick = onOpenDevices
                        )
                        RowDivider()
                        QuickRow(
                            icon = "警",
                            color = IOSOrange,
                            title = "告警",
                            subtitle = "$unresolved ${s.dashboard.alertsSubtitle}",
                            onClick = onOpenAlerts
                        )
                    }

                    Row(
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(top = 14.dp, bottom = 2.dp),
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        LieEyebrow(s.dashboard.todaySummary)
                        Spacer(modifier = Modifier.weight(1f))
                        com.aura.android.ui.screens.trend.TrendEntryHint(
                            modifier = Modifier.clickable(onClick = onOpenTrend)
                        )
                    }
                    SummaryCard(
                        summary = dashboard.todaySummary,
                        trend = dashboard.recentTrend
                    )

                    SectionTitle(s.dashboard.vitalsTrend)
                    VitalsTrendCard(dashboard)

                    if (recentAlerts.isNotEmpty()) {
                        LiveSectionHeader(title = s.dashboard.recentAlerts)
                        Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
                            recentAlerts.forEach { alert ->
                                AlertItem(alert = alert)
                            }
                        }
                    }

                    if (recentDevices.isNotEmpty()) {
                        LiveSectionHeader(
                            title = s.dashboard.recentDevices,
                            actionLabel = s.dashboard.addDevice,
                            onAction = { onOpenDevices() }
                        )
                        Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
                            recentDevices.forEach { device ->
                                DeviceItem(
                                    device = device,
                                    onDelete = {
                                        scope.launch {
                                            DeviceRepository.remove(device.id)
                                            viewModel.loadDashboard(showLoading = false)
                                        }
                                    }
                                )
                            }
                        }
                    }

                    Spacer(modifier = Modifier.height(112.dp))
                }
            }
        }
    }
}

// 实时区块标题
@Composable
private fun LiveSectionHeader(
    title: String,
    actionLabel: String? = null,
    onAction: (() -> Unit)? = null
) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(top = 14.dp, bottom = 2.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        LieEyebrow(title)
        Spacer(modifier = Modifier.width(8.dp))
        Box(
            modifier = Modifier
                .size(6.dp)
                .clip(CircleShape)
                .background(ChartData.copy(alpha = 0.85f))
        )
        Spacer(modifier = Modifier.width(5.dp))
        Text(
            text = "实时",
            style = MaterialTheme.typography.labelSmall.copy(color = Hint, fontSize = 10.sp)
        )
        Spacer(modifier = Modifier.weight(1f))
        if (actionLabel != null && onAction != null) {
            Text(
                text = actionLabel,
                style = MaterialTheme.typography.labelSmall.copy(
                    color = ChartHero,
                    fontSize = 11.sp,
                    fontWeight = FontWeight.Bold
                ),
                modifier = Modifier
                    .clip(RoundedCornerShape(50.dp))
                    .clickable(onClick = onAction)
                    .padding(horizontal = 8.dp, vertical = 4.dp)
            )
        }
    }
}

private fun axisLabel(raw: String?): String {
    if (raw.isNullOrBlank()) return ""
    return try {
        java.time.format.DateTimeFormatter
            .ofPattern("HH:mm")
            .withZone(java.time.ZoneId.systemDefault())
            .format(java.time.Instant.parse(raw))
    } catch (e: Exception) {
        raw.take(5)
    }
}

private fun formatRecordTime(raw: String): String {
    return try {
        java.time.format.DateTimeFormatter
            .ofPattern("MM-dd HH:mm")
            .withZone(java.time.ZoneId.systemDefault())
            .format(java.time.Instant.parse(raw))
    } catch (e: Exception) {
        raw.take(16).replace("T", " ")
    }
}

@Composable
private fun DashboardNav(updatedAt: Long?, onRefresh: () -> Unit, onOpenChat: () -> Unit) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .background(
                androidx.compose.ui.graphics.Brush.verticalGradient(
                    listOf(
                        Surface.copy(alpha = 1.00f),
                        Surface.copy(alpha = 0.97f),
                        Surface.copy(alpha = 0.90f)
                    )
                )
            )
            .statusBarsPadding()
            .padding(start = 20.dp, end = 12.dp, top = 8.dp, bottom = 14.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Column(modifier = Modifier.weight(1f)) {
            LieEyebrow(
                text = SimpleDateFormat("M月d日 EEEE", Locale.CHINA)
                    .format(Date())
                    .uppercase()
            )
            Spacer(modifier = Modifier.height(6.dp))
            Text(
                text = "今天",
                style = MaterialTheme.typography.displayLarge.copy(
                    color = OnSurface,
                    fontSize = 42.sp
                )
            )
        }
        if (updatedAt != null) {
            Text(
                text = SimpleDateFormat("HH:mm", Locale.getDefault()).format(Date(updatedAt)),
                style = MaterialTheme.typography.labelSmall.copy(color = Hint)
            )
        }
        Spacer(modifier = Modifier.width(8.dp))
        Box(
            modifier = Modifier
                .size(34.dp)
                .clip(CircleShape)
                .background(PrimaryContainer)
                .clickable(onClick = onOpenChat),
            contentAlignment = Alignment.Center
        ) {
            Text(
                text = "AI",
                style = MaterialTheme.typography.labelSmall.copy(
                    color = PrimaryBlue,
                    fontWeight = FontWeight.Bold
                )
            )
        }
        IconButton(onClick = onRefresh) {
            Icon(Icons.Default.Refresh, contentDescription = "刷新", tint = OnSurface)
        }
    }
}

@Composable
private fun HeroHeartCard(hr: Double?, statusText: String, live: Boolean) {
    LieReveal(index = 0) {
    LieCard {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Column(modifier = Modifier.weight(1f)) {
                LieEyebrow(if (live) "实时心率 / LIVE HR" else "静息心率 / RESTING HR")
                Spacer(modifier = Modifier.height(12.dp))
                Row(verticalAlignment = Alignment.Bottom) {
                    AnimatedCounter(
                        value = hr?.toFloat(),
                        style = MaterialTheme.typography.displayLarge.copy(fontSize = 58.sp),
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
                    color = if (live) Success else Hint,
                    text = statusText,
                    pulse = live
                )
            }
            LieGauge(
                fraction = ((hr ?: 0.0).toFloat() / 120f).coerceIn(0f, 1f),
                modifier = Modifier.size(86.dp),
                color = ChartData
            )
        }
    }
    }
}

@Composable
private fun MetricStrip(
    respiration: Double?,
    focus: Double?,
    stress: Double?
) {
    LieReveal(index = 1) {
    LieCard {
        Row(modifier = Modifier.fillMaxWidth()) {
            StripItem("呼吸", respiration?.let { "${it.toInt()}" } ?: "--", "次/分", Modifier.weight(1f))
            VerticalLine()
            StripItem("专注", focus?.let { "${it.toInt()}" } ?: "--", "分", Modifier.weight(1f))
            VerticalLine()
            StripItem("压力", stress?.let { "${it.toInt()}" } ?: "--", "", Modifier.weight(1f))
        }
    }
    }
}

@Composable
private fun StripItem(label: String, value: String, unit: String, modifier: Modifier = Modifier) {
    Column(modifier = modifier, horizontalAlignment = Alignment.CenterHorizontally) {
        Text(label, style = MaterialTheme.typography.labelSmall.copy(color = Hint))
        Spacer(modifier = Modifier.height(8.dp))
        Row(verticalAlignment = Alignment.Bottom) {
            Text(
                text = value,
                style = MaterialTheme.typography.headlineMedium.copy(
                    color = OnSurface,
                    fontSize = 24.sp
                )
            )
            if (unit.isNotBlank()) {
                Text(
                    text = unit,
                    style = MaterialTheme.typography.labelSmall.copy(color = Hint),
                    modifier = Modifier.padding(start = 3.dp, bottom = 3.dp)
                )
            }
        }
    }
}

@Composable
private fun VerticalLine() {
    Box(
        modifier = Modifier
            .width(1.dp)
            .height(40.dp)
            .background(Hairline.copy(alpha = 0.7f))
    )
}

@Composable
private fun SectionTitle(text: String) {
    LieEyebrow(
        text = text,
        modifier = Modifier.padding(top = 14.dp, bottom = 2.dp)
    )
}

@Composable
private fun GroupCard(content: @Composable () -> Unit) {
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(24.dp))
            .background(Paper)
    ) {
        content()
    }
}

@Composable
private fun RowDivider() {
    HorizontalDivider(
        color = Hairline.copy(alpha = 0.65f),
        thickness = 1.dp,
        modifier = Modifier.padding(start = 42.dp)
    )
}

@Composable
private fun QuickRow(
    icon: String,
    color: Color,
    title: String,
    subtitle: String,
    onClick: () -> Unit
) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .clickable(onClick = onClick)
            .padding(horizontal = 20.dp, vertical = 15.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Box(
            modifier = Modifier
                .size(8.dp)
                .clip(CircleShape)
                .background(color)
        )
        Spacer(modifier = Modifier.width(14.dp))
        Column(modifier = Modifier.weight(1f)) {
            Text(
                text = title,
                style = MaterialTheme.typography.titleMedium.copy(
                    color = OnSurface,
                    fontWeight = FontWeight.SemiBold
                )
            )
            Spacer(modifier = Modifier.height(2.dp))
            Text(
                text = subtitle,
                style = MaterialTheme.typography.labelSmall.copy(color = Hint)
            )
        }
        Text(
            text = "›",
            style = MaterialTheme.typography.titleLarge.copy(color = Hint)
        )
    }
}

@Composable
private fun SummaryCard(
    summary: com.aura.android.data.model.TodaySummary,
    trend: List<TrendPoint>
) {
    LieCard {
        Column {
            if (summary.recordCount == 0) {
                // 无入库记录时的说明
                Text(
                    text = "今日暂无入库记录",
                    style = MaterialTheme.typography.bodyMedium.copy(color = OnSurface)
                )
                Spacer(modifier = Modifier.height(4.dp))
                Text(
                    text = "实时数据不会自动写入历史库，趋势页从有记录后开始累计。",
                    style = MaterialTheme.typography.labelSmall.copy(color = Hint)
                )
            } else {
                Row(modifier = Modifier.fillMaxWidth()) {
                    Stat("${summary.recordCount}", "记录数", Modifier.weight(1f))
                    Stat(summary.averageHeartRate?.let { "${it.toInt()}" } ?: "--", "平均心率", Modifier.weight(1f))
                    Stat(summary.averageRespirationRate?.let { "${it.toInt()}" } ?: "--", "平均呼吸", Modifier.weight(1f))
                    Stat("${summary.averageFocus.toInt()}", "平均专注", Modifier.weight(1f))
                }
            }
            if (trend.isNotEmpty()) {
                Spacer(modifier = Modifier.height(18.dp))
                Box(
                    modifier = Modifier
                        .fillMaxWidth()
                        .height(1.dp)
                        .background(Hairline.copy(alpha = 0.6f))
                )
                Spacer(modifier = Modifier.height(16.dp))
                LieTrendChart(
                    values = trend.map { it.averageFocus.toFloat() },
                    modifier = Modifier
                        .fillMaxWidth()
                        .height(132.dp),
                    color = ChartData,
                    yMin = 0f,
                    yMax = 100f,
                    yTicks = listOf(0f, 50f, 100f),
                    xLabels = listOf("更早", "现在")
                )
            }
        }
    }
}

@Composable
private fun Stat(value: String, label: String, modifier: Modifier = Modifier) {
    Column(
        modifier = modifier,
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        Text(
            text = value,
            style = MaterialTheme.typography.titleLarge.copy(
                color = OnSurface,
                fontSize = 22.sp,
                fontWeight = FontWeight.SemiBold
            )
        )
        Spacer(modifier = Modifier.height(2.dp))
        Text(label, style = MaterialTheme.typography.labelSmall.copy(color = Hint))
    }
}

@Composable
private fun VitalsTrendCard(dashboard: MobileDashboard) {
    var selectedMetric by remember { mutableStateOf(VitalsMetric.HEART_RATE) }
    val lineColor = when (selectedMetric) {
        VitalsMetric.HEART_RATE -> ChartData
        VitalsMetric.RESPIRATION -> Color(0xFF5A7049)
        VitalsMetric.STRESS -> Color(0xFF929960)
        VitalsMetric.FATIGUE -> Color(0xFFACAD79)
        VitalsMetric.FOCUS -> ChartHero
    }

    LieCard {
        Column {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(18.dp)
            ) {
                VitalsMetric.entries.forEach { metric ->
                    val selected = selectedMetric == metric
                    Column(
                        modifier = Modifier
                            .clickable { selectedMetric = metric }
                            .padding(vertical = 4.dp),
                        horizontalAlignment = Alignment.CenterHorizontally
                    ) {
                        Text(
                            text = metric.label,
                            style = MaterialTheme.typography.labelMedium.copy(
                                color = if (selected) OnSurface else Hint,
                                fontWeight = if (selected) FontWeight.Bold else FontWeight.Normal
                            )
                        )
                        Spacer(modifier = Modifier.height(5.dp))
                        Box(
                            modifier = Modifier
                                .width(if (selected) 18.dp else 0.dp)
                                .height(2.dp)
                                .background(if (selected) ChartHero else Color.Transparent)
                        )
                    }
                }
            }
            Spacer(modifier = Modifier.height(14.dp))
            val points = dashboard.vitalsHistory
            val values = points.mapNotNull { p ->
                when (selectedMetric) {
                    VitalsMetric.HEART_RATE -> p.heartRate
                    VitalsMetric.RESPIRATION -> p.respirationRate
                    VitalsMetric.STRESS -> p.stressLevel
                    VitalsMetric.FATIGUE -> p.fatigueLevel
                    VitalsMetric.FOCUS -> p.focusScore
                }?.toFloat()
            }
            // 横轴使用真实采样时间
            val axisLabels = if (points.size >= 2) {
                listOf(
                    axisLabel(points.first().time),
                    axisLabel(points[points.size / 2].time),
                    axisLabel(points.last().time)
                )
            } else {
                listOf("", "", "")
            }
            // 评分指标用柱状图
            val isScoreMetric = selectedMetric == VitalsMetric.STRESS ||
                selectedMetric == VitalsMetric.FATIGUE ||
                selectedMetric == VitalsMetric.FOCUS
            if (values.isEmpty()) {
                Text(
                    text = "暂无历史记录",
                    style = MaterialTheme.typography.labelSmall.copy(color = Hint)
                )
            } else if (isScoreMetric) {
                LieEyebrow("评分 / SCORE · 0–100")
                Spacer(modifier = Modifier.height(8.dp))
                LieBarChart(
                    values = values,
                    modifier = Modifier
                        .fillMaxWidth()
                        .height(150.dp),
                    color = lineColor,
                    yMax = 100f,
                    yTicks = listOf(0f, 50f, 100f),
                    xLabels = axisLabels
                )
            } else {
                val lo = (values.minOrNull() ?: 0f) - 3f
                val hi = (values.maxOrNull() ?: 100f) + 3f
                LieTrendChart(
                    values = values,
                    modifier = Modifier
                        .fillMaxWidth()
                        .height(150.dp),
                    color = lineColor,
                    yMin = lo,
                    yMax = hi,
                    yTicks = listOf(lo, (lo + hi) / 2f, hi),
                    xLabels = axisLabels
                )
            }
        }
    }
}

@Composable
private fun DashboardLoading(modifier: Modifier = Modifier) {
    Column(
        modifier = modifier,
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center
    ) {
        CircularProgressIndicator(color = PrimaryBlue)
        Spacer(modifier = Modifier.height(12.dp))
        Text("正在加载…", color = Muted)
    }
}

@Composable
private fun DashboardError(message: String, onRetry: () -> Unit, modifier: Modifier = Modifier) {
    Column(
        modifier = modifier,
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center
    ) {
        Text(message, color = MaterialTheme.colorScheme.error)
        Spacer(modifier = Modifier.height(12.dp))
        TextButton(onClick = onRetry) {
            Text("重试", color = PrimaryBlue)
        }
    }
}
