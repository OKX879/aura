package com.aura.android.ui.screens.chat

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.ime
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.automirrored.filled.Send
import androidx.compose.material.icons.filled.SmartToy
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextField
import androidx.compose.material3.TextFieldDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aura.android.data.ai.VitalsSnapshot
import com.aura.android.ui.components.LieCard
import com.aura.android.ui.components.LieEyebrow
import com.aura.android.ui.components.LieReveal
import com.aura.android.ui.components.LieRule
import com.aura.android.ui.components.LieStatusDot
import com.aura.android.ui.theme.ChartData
import com.aura.android.ui.theme.ChartHero
import com.aura.android.ui.theme.Hint
import com.aura.android.ui.theme.IOSLabel
import com.aura.android.ui.theme.IOSSecondaryLabel
import com.aura.android.ui.theme.Muted
import com.aura.android.ui.theme.OnSurface
import com.aura.android.ui.theme.Paper
import com.aura.android.ui.theme.PrimaryContainer
import com.aura.android.ui.theme.Surface
import com.aura.android.ui.theme.SurfaceLow
import java.time.Instant
import java.time.ZoneId
import java.time.format.DateTimeFormatter

// AI 分析页
@OptIn(ExperimentalLayoutApi::class)
@Composable
fun ChatScreen(
    viewModel: ChatViewModel,
    onBack: () -> Unit = {},
    onOpenSettings: () -> Unit = {}
) {
    val listState = rememberLazyListState()
    val s = com.aura.android.ui.i18n.LocalStrings.current

    // 返回时重新读取配置
    LaunchedEffect(Unit) { viewModel.reload() }

    LaunchedEffect(viewModel.messages.size) {
        if (viewModel.messages.isNotEmpty()) {
            listState.animateScrollToItem(viewModel.messages.size - 1)
        }
    }

    val density = LocalDensity.current
    val imeBottom = WindowInsets.ime.getBottom(density)
    val dockBottom = if (imeBottom > 0) 10.dp else 96.dp

    Column(
        modifier = Modifier
            .fillMaxSize()
            .background(androidx.compose.ui.graphics.Color.Transparent)
    ) {
        ChatTopBar(
            modelLabel = viewModel.modelLabel,
            configured = viewModel.configured,
            onBack = onBack
        )

        LazyColumn(
            modifier = Modifier
                .weight(1f)
                .fillMaxWidth(),
            state = listState,
            contentPadding = PaddingValues(start = 20.dp, end = 20.dp, top = 4.dp, bottom = 16.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            item {
                LieReveal(0) {
                    SnapshotCard(
                        snapshot = viewModel.snapshot,
                        configured = viewModel.configured,
                        webSearchEnabled = viewModel.webSearchEnabled,
                        onToggleWebSearch = viewModel::toggleWebSearch,
                        onOpenSettings = onOpenSettings
                    )
                }
            }

            if (viewModel.messages.size <= 1) {
                item {
                    LieReveal(1) {
                        QuickQuestionStrip(
                            enabled = !viewModel.isLoading,
                            onPick = viewModel::sendQuickQuestion
                        )
                    }
                }
            }

            itemsIndexed(viewModel.messages) { _, message ->
                MessageBubble(item = message)
            }

            if (viewModel.isLoading) {
                item {
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Box(
                            modifier = Modifier
                                .size(30.dp)
                                .clip(CircleShape)
                                .background(PrimaryContainer),
                            contentAlignment = Alignment.Center
                        ) {
                            Icon(
                                imageVector = Icons.Default.SmartToy,
                                contentDescription = null,
                                tint = ChartData,
                                modifier = Modifier.size(17.dp)
                            )
                        }
                        Spacer(modifier = Modifier.width(8.dp))
                        Box(
                            modifier = Modifier
                                .clip(RoundedCornerShape(topStart = 4.dp, topEnd = 18.dp, bottomEnd = 18.dp, bottomStart = 18.dp))
                                .background(Paper)
                                .padding(horizontal = 14.dp, vertical = 12.dp)
                        ) {
                            LieStatusDot(
                                color = ChartHero,
                                text = s.chat.analyzing,
                                pulse = true
                            )
                        }
                    }
                }
            }
        }

        ChatInputBar(
            text = viewModel.inputText,
            isLoading = viewModel.isLoading,
            onTextChange = viewModel::updateInput,
            onSend = viewModel::sendMessage,
            bottomSpace = dockBottom
        )
    }
}

@Composable
private fun ChatTopBar(modelLabel: String, configured: Boolean, onBack: () -> Unit) {
    val s = com.aura.android.ui.i18n.LocalStrings.current
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .background(
                Brush.verticalGradient(
                    listOf(
                        Surface.copy(alpha = 1.00f),
                        Surface.copy(alpha = 0.97f),
                        Surface.copy(alpha = 0.90f)
                    )
                )
            )
            .statusBarsPadding()
            .padding(start = 16.dp, end = 20.dp, top = 8.dp, bottom = 12.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Box(
            modifier = Modifier
                .size(36.dp)
                .clip(CircleShape)
                .background(Paper)
                .clickable(onClick = onBack),
            contentAlignment = Alignment.Center
        ) {
            Icon(
                imageVector = Icons.AutoMirrored.Filled.ArrowBack,
                contentDescription = "返回",
                tint = OnSurface,
                modifier = Modifier.size(19.dp)
            )
        }
        Spacer(modifier = Modifier.width(12.dp))
        Column(modifier = Modifier.weight(1f)) {
            LieEyebrow("AI ANALYSIS")
            Spacer(modifier = Modifier.height(3.dp))
            Text(
                text = s.chat.title,
                style = MaterialTheme.typography.headlineMedium.copy(
                    color = OnSurface,
                    fontSize = 27.sp,
                    fontWeight = FontWeight.Black
                )
            )
        }
        Column(horizontalAlignment = Alignment.End) {
            Row(
                modifier = Modifier
                    .clip(RoundedCornerShape(50))
                    .background(PrimaryContainer)
                    .padding(horizontal = 11.dp, vertical = 6.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                Box(
                    modifier = Modifier
                        .size(6.dp)
                        .clip(CircleShape)
                        .background(if (configured) ChartData else ChartHero)
                )
                Spacer(modifier = Modifier.width(6.dp))
                Text(
                    text = modelLabel,
                    style = MaterialTheme.typography.labelSmall.copy(
                        color = OnSurface,
                        fontSize = 11.sp
                    ),
                    maxLines = 1
                )
            }
            Spacer(modifier = Modifier.height(4.dp))
            Text(
                text = if (configured) s.chat.connected else s.chat.notConfigured,
                style = MaterialTheme.typography.labelSmall.copy(
                    color = if (configured) IOSSecondaryLabel else ChartHero,
                    fontSize = 10.sp
                )
            )
        }
    }
}

@Composable
private fun SnapshotCard(
    snapshot: VitalsSnapshot?,
    configured: Boolean,
    webSearchEnabled: Boolean,
    onToggleWebSearch: () -> Unit,
    onOpenSettings: () -> Unit
) {
    LieCard {
        Row(verticalAlignment = Alignment.CenterVertically) {
            LieEyebrow("数据快照 / SNAPSHOT")
            Spacer(modifier = Modifier.weight(1f))
            if (snapshot != null) {
                LieStatusDot(
                    color = ChartData,
                    text = "${snapshot.todaySummary.recordCount} 条记录"
                )
            }
        }
        Spacer(modifier = Modifier.height(14.dp))
        Row(modifier = Modifier.fillMaxWidth()) {
            SnapshotCell("心率", snapshot?.currentHeartRate, "次/分", Modifier.weight(1f))
            SnapshotCell("专注", snapshot?.currentFocus, "分", Modifier.weight(1f))
            SnapshotCell("压力", snapshot?.currentStress, "分", Modifier.weight(1f))
            SnapshotCell("呼吸", snapshot?.currentRespiration, "次/分", Modifier.weight(1f))
        }
        Spacer(modifier = Modifier.height(14.dp))
        LieRule()
        Spacer(modifier = Modifier.height(10.dp))
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(
                text = snapshot?.let { "数据截至 ${formatClock(it)}，随时间更新" } ?: "正在读取体征数据…",
                style = MaterialTheme.typography.labelSmall.copy(color = Hint, fontSize = 10.sp),
                modifier = Modifier.weight(1f)
            )
            Row(
                modifier = Modifier
                    .clip(RoundedCornerShape(50))
                    .background(if (webSearchEnabled) PrimaryContainer else SurfaceLow)
                    .clickable(onClick = onToggleWebSearch)
                    .padding(horizontal = 10.dp, vertical = 5.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                Box(
                    modifier = Modifier
                        .size(5.dp)
                        .clip(CircleShape)
                        .background(if (webSearchEnabled) ChartData else Hint)
                )
                Spacer(modifier = Modifier.width(5.dp))
                Text(
                    text = if (webSearchEnabled) "联网 开" else "联网 关",
                    style = MaterialTheme.typography.labelSmall.copy(
                        color = if (webSearchEnabled) OnSurface else Muted,
                        fontSize = 10.sp
                    )
                )
            }
            if (!configured) {
                Spacer(modifier = Modifier.width(10.dp))
                Text(
                    text = "去配置接口 ›",
                    style = MaterialTheme.typography.labelSmall.copy(
                        color = ChartHero,
                        fontSize = 10.sp,
                        fontWeight = FontWeight.Bold
                    ),
                    modifier = Modifier.clickable(onClick = onOpenSettings)
                )
            }
        }
    }
}

@Composable
private fun SnapshotCell(label: String, value: Float?, unit: String, modifier: Modifier = Modifier) {
    Column(modifier = modifier) {
        Text(
            text = value?.let { String.format("%.0f", it) } ?: "--",
            style = MaterialTheme.typography.headlineMedium.copy(
                color = if (value == null) Hint else IOSLabel,
                fontSize = 24.sp,
                fontWeight = FontWeight.Bold
            )
        )
        Spacer(modifier = Modifier.height(2.dp))
        Text(
            text = "$label · $unit",
            style = MaterialTheme.typography.labelSmall.copy(color = Muted, fontSize = 10.sp)
        )
    }
}

@Composable
private fun QuickQuestionStrip(enabled: Boolean, onPick: (String) -> Unit) {
    val s = com.aura.android.ui.i18n.LocalStrings.current
    val questions = s.chat.suggestions
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .horizontalScroll(rememberScrollState()),
        horizontalArrangement = Arrangement.spacedBy(8.dp)
    ) {
        questions.forEach { q ->
            Box(
                modifier = Modifier
                    .clip(RoundedCornerShape(50))
                    .background(Paper)
                    .clickable(enabled = enabled) { onPick(q) }
                    .padding(horizontal = 14.dp, vertical = 9.dp)
            ) {
                Text(
                    text = q,
                    style = MaterialTheme.typography.labelMedium.copy(
                        color = if (enabled) ChartData else Hint,
                        fontSize = 12.sp
                    )
                )
            }
        }
    }
}

@Composable
private fun MessageBubble(item: ChatItem) {
    val isUser = item.role == "user"
    Column(modifier = Modifier.fillMaxWidth()) {
        if (!isUser) {
            Row(
                modifier = Modifier.padding(start = 2.dp, bottom = 5.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                Icon(
                    imageVector = Icons.Default.SmartToy,
                    contentDescription = null,
                    tint = if (item.tag == "接口异常") ChartHero else ChartData,
                    modifier = Modifier.size(13.dp)
                )
                Spacer(modifier = Modifier.width(5.dp))
                Text(
                    text = item.tag ?: "AURA",
                    style = MaterialTheme.typography.labelSmall.copy(
                        color = if (item.tag == "接口异常") ChartHero else Hint,
                        fontSize = 10.sp
                    )
                )
            }
        }
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = if (isUser) Arrangement.End else Arrangement.Start
        ) {
            Box(
                modifier = Modifier
                    .widthIn(max = 300.dp)
                    .clip(
                        RoundedCornerShape(
                            topStart = if (isUser) 20.dp else 6.dp,
                            topEnd = 20.dp,
                            bottomEnd = if (isUser) 6.dp else 20.dp,
                            bottomStart = 20.dp
                        )
                    )
                    .background(if (isUser) ChartData else Paper)
                    .padding(horizontal = 15.dp, vertical = 11.dp)
            ) {
                Text(
                    text = item.content,
                    style = MaterialTheme.typography.bodyMedium.copy(
                        fontSize = 14.sp,
                        lineHeight = 21.sp
                    ),
                    color = if (isUser) Paper else IOSLabel
                )
            }
        }
        if (!isUser && item.sources.isNotEmpty()) {
            Column(modifier = Modifier.padding(start = 4.dp, top = 6.dp)) {
                Text(
                    text = "参考 ${item.sources.size} 条网页",
                    style = MaterialTheme.typography.labelSmall.copy(color = Hint, fontSize = 10.sp)
                )
                item.sources.take(3).forEach { source ->
                    Text(
                        text = "· ${source.title}",
                        style = MaterialTheme.typography.labelSmall.copy(
                            color = IOSSecondaryLabel,
                            fontSize = 10.sp
                        ),
                        maxLines = 1,
                        overflow = androidx.compose.ui.text.style.TextOverflow.Ellipsis,
                        modifier = Modifier.padding(top = 2.dp)
                    )
                }
            }
        }
    }
}

@Composable
private fun ChatInputBar(
    text: String,
    isLoading: Boolean,
    onTextChange: (String) -> Unit,
    onSend: () -> Unit,
    bottomSpace: androidx.compose.ui.unit.Dp
) {
    val s = com.aura.android.ui.i18n.LocalStrings.current
    Box(
        modifier = Modifier
            .fillMaxWidth()
            .navigationBarsPadding()
            .imePadding()
            .padding(start = 20.dp, end = 20.dp, bottom = bottomSpace, top = 6.dp)
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .clip(RoundedCornerShape(26.dp))
                .background(Paper)
                .padding(start = 8.dp, end = 8.dp, top = 6.dp, bottom = 6.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            TextField(
                value = text,
                onValueChange = onTextChange,
                placeholder = {
                    Text(
                        text = s.chat.placeholder,
                        style = MaterialTheme.typography.bodySmall.copy(color = Hint, fontSize = 13.sp)
                    )
                },
                modifier = Modifier.weight(1f),
                shape = RoundedCornerShape(22.dp),
                colors = TextFieldDefaults.colors(
                    focusedContainerColor = Color.Transparent,
                    unfocusedContainerColor = Color.Transparent,
                    disabledContainerColor = Color.Transparent,
                    focusedIndicatorColor = Color.Transparent,
                    unfocusedIndicatorColor = Color.Transparent,
                    disabledIndicatorColor = Color.Transparent,
                    cursorColor = ChartData
                ),
                textStyle = MaterialTheme.typography.bodyMedium.copy(fontSize = 14.sp, color = IOSLabel),
                maxLines = 4,
                keyboardOptions = KeyboardOptions(imeAction = ImeAction.Send),
                keyboardActions = KeyboardActions(onSend = { onSend() })
            )
            Box(
                modifier = Modifier
                    .size(40.dp)
                    .clip(CircleShape)
                    .background(
                        if (text.isNotBlank() && !isLoading) ChartData
                        else SurfaceLow
                    )
                    .clickable(enabled = text.isNotBlank() && !isLoading, onClick = onSend),
                contentAlignment = Alignment.Center
            ) {
                if (isLoading) {
                    CircularProgressIndicator(
                        modifier = Modifier.size(17.dp),
                        strokeWidth = 2.dp,
                        color = ChartData
                    )
                } else {
                    Icon(
                        imageVector = Icons.AutoMirrored.Filled.Send,
                        contentDescription = "发送",
                        tint = if (text.isNotBlank()) Paper else Hint,
                        modifier = Modifier.size(18.dp)
                    )
                }
            }
        }
    }
}

private fun formatClock(snapshot: VitalsSnapshot): String = try {
    val raw = snapshot.latest?.recordedAt
    if (raw == null) "--:--"
    else DateTimeFormatter.ofPattern("HH:mm")
        .withZone(ZoneId.systemDefault())
        .format(Instant.parse(raw))
} catch (e: Exception) {
    "--:--"
}
