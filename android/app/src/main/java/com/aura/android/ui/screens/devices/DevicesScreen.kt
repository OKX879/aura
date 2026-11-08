package com.aura.android.ui.screens.devices

import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Canvas
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
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.Sensors
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.rememberModalBottomSheetState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aura.android.data.api.ApiClient
import com.aura.android.data.api.ApiResult
import com.aura.android.data.model.Device
import com.aura.android.ui.components.AddDeviceDialog
import com.aura.android.ui.components.AuraCard
import com.aura.android.ui.theme.Border
import com.aura.android.ui.theme.ErrorRed
import com.aura.android.ui.theme.Hairline
import com.aura.android.ui.theme.Hint
import com.aura.android.ui.theme.Muted
import com.aura.android.ui.theme.OnSurface
import com.aura.android.ui.theme.Paper
import com.aura.android.ui.theme.PrimaryBlue
import com.aura.android.ui.theme.SecondaryContainer
import com.aura.android.ui.theme.Success
import com.aura.android.ui.theme.SuccessSoft
import com.aura.android.ui.theme.Surface
import com.aura.android.ui.theme.SurfaceContainer
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

// 设备页
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun DevicesScreen(viewModel: DevicesViewModel) {
    var selected by remember { mutableStateOf<Device?>(null) }
    var showAdd by remember { mutableStateOf(false) }
    val s = com.aura.android.ui.i18n.LocalStrings.current
    val success = viewModel.uiState as? ApiResult.Success<List<Device>>
    val devices = success?.data.orEmpty()
    val online = devices.count { it.status == "online" }
    val sheetState = rememberModalBottomSheetState(skipPartiallyExpanded = true)

    // 每 10 秒静默刷新设备状态
    LaunchedEffect(Unit) {
        while (true) {
            viewModel.loadDevices(silent = true)
            kotlinx.coroutines.delay(10_000)
        }
    }

    Scaffold(
        containerColor = Color.Transparent,
        topBar = {
            DevicesNav(
                s = s,
                count = devices.size,
                online = online,
                onRefresh = { viewModel.loadDevices() },
                onAdd = { showAdd = true }
            )
        }
    ) { padding ->
        when (val state = viewModel.uiState) {
            is ApiResult.Loading -> {
                DeviceSkeleton(modifier = Modifier.fillMaxSize().padding(padding))
            }
            is ApiResult.Error -> {
                DeviceError(
                    message = state.message,
                    onRetry = { viewModel.loadDevices() },
                    modifier = Modifier.fillMaxSize().padding(padding)
                )
            }
            is ApiResult.Success -> {
                var appeared by remember { mutableStateOf(false) }
                LaunchedEffect(Unit) { appeared = true }
                val alpha by animateFloatAsState(
                    targetValue = if (appeared) 1f else 0f,
                    animationSpec = tween(350),
                    label = "devicesAlpha"
                )
                LazyColumn(
                    modifier = Modifier
                        .fillMaxSize()
                        .padding(padding)
                        .padding(horizontal = 24.dp)
                        .graphicsLayer { this.alpha = alpha },
                    verticalArrangement = Arrangement.spacedBy(12.dp)
                ) {
                    item {
                        OnlineOverview(online = online, total = state.data.size)
                    }
                    if (state.data.isEmpty()) {
                        item { EmptyDevices(onRefresh = { viewModel.loadDevices() }) }
                    } else {
                        items(state.data, key = { it.id }) { device ->
                            DeviceCard(device = device, onClick = { selected = device })
                        }
                    }
                    item { Spacer(modifier = Modifier.height(16.dp)) }
                }
            }
        }
    }

    selected?.let { device ->
        ModalBottomSheet(
            onDismissRequest = { selected = null },
            sheetState = sheetState,
            containerColor = Paper,
            shape = RoundedCornerShape(topStart = 26.dp, topEnd = 26.dp)
        ) {
            DeviceDetail(
                device = device,
                onDelete = {
                    selected = null
                    viewModel.deleteDevice(device.id)
                }
            )
        }
    }

    if (showAdd) {
        AddDeviceDialog(
            defaultEndpoint = ApiClient.currentBaseUrl.removeSuffix("/"),
            discovered = viewModel.discovered,
            scanning = viewModel.scanning,
            onScan = viewModel::scanNearby,
            onDismiss = { showAdd = false },
            onSubmit = { name, endpoint, code ->
                viewModel.pairAndAdd(name, endpoint, code)
            },
            s = s
        )
    }
}

@Composable
private fun DevicesNav(
    s: com.aura.android.ui.i18n.Strings,
    count: Int,
    online: Int,
    onRefresh: () -> Unit,
    onAdd: () -> Unit
) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .background(Surface)
            .statusBarsPadding()
            .padding(start = 24.dp, end = 12.dp, top = 10.dp, bottom = 8.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Column(modifier = Modifier.weight(1f)) {
            Text(
                text = s.devices.title,
                style = MaterialTheme.typography.titleLarge.copy(color = OnSurface)
            )
            Spacer(modifier = Modifier.height(2.dp))
            Text(
                text = String.format(s.devices.onlineCount, online, count),
                style = MaterialTheme.typography.labelSmall.copy(color = Hint)
            )
        }
        Text(
            text = s.devices.add,
            style = MaterialTheme.typography.labelSmall.copy(
                color = PrimaryBlue,
                fontWeight = FontWeight.SemiBold
            ),
            modifier = Modifier
                .clip(RoundedCornerShape(30.dp))
                .background(SecondaryContainer)
                .clickable(onClick = onAdd)
                .padding(horizontal = 10.dp, vertical = 5.dp)
        )
        IconButton(onClick = onRefresh) {
            Icon(Icons.Default.Refresh, contentDescription = s.common.refresh, tint = OnSurface)
        }
    }
}

@Composable
private fun OnlineOverview(online: Int, total: Int) {
    val s = com.aura.android.ui.i18n.LocalStrings.current
    AuraCard(modifier = Modifier.padding(top = 12.dp)) {
        Column {
            Row(verticalAlignment = Alignment.Bottom) {
                Text(
                    text = "$online",
                    style = MaterialTheme.typography.displayMedium.copy(
                        color = OnSurface,
                        fontSize = 40.sp,
                        fontWeight = FontWeight.Bold
                    )
                )
                Text(
                    text = " / $total ${s.common.online}",
                    style = MaterialTheme.typography.bodyMedium.copy(color = Muted),
                    modifier = Modifier.padding(bottom = 6.dp)
                )
                Spacer(modifier = Modifier.weight(1f))
                Text(
                    text = if (online == total && total > 0) s.devices.allOnline
                    else s.devices.partiallyOffline,
                    style = MaterialTheme.typography.labelSmall.copy(
                        color = if (online == total && total > 0) Success else Muted
                    )
                )
            }
            Spacer(modifier = Modifier.height(14.dp))
            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .height(6.dp)
                    .clip(RoundedCornerShape(3.dp))
                    .background(SurfaceContainer)
            ) {
                Box(
                    modifier = Modifier
                        .fillMaxWidth(
                            if (total > 0) (online.toFloat() / total).coerceIn(0f, 1f) else 0f
                        )
                        .height(6.dp)
                        .clip(RoundedCornerShape(3.dp))
                        .background(PrimaryBlue)
                )
            }
            Spacer(modifier = Modifier.height(10.dp))
            Text(
                text = s.devices.refreshedAt +
                    SimpleDateFormat("HH:mm:ss", Locale.getDefault()).format(Date()),
                style = MaterialTheme.typography.labelSmall.copy(color = Hint)
            )
        }
    }
}

@Composable
private fun DeviceCard(device: Device, onClick: () -> Unit) {
    val s = com.aura.android.ui.i18n.LocalStrings.current
    val online = device.status == "online"
    val statusColor = when (device.status) {
        "online" -> Success
        "error" -> ErrorRed
        else -> Muted
    }
    val transition = rememberInfiniteTransition(label = "devicePulse")
    val pulse by transition.animateFloat(
        initialValue = 0.35f,
        targetValue = 1f,
        animationSpec = infiniteRepeatable(
            animation = tween(1400),
            repeatMode = RepeatMode.Reverse
        ),
        label = "pulse"
    )

    AuraCard(
        modifier = Modifier.clickable(onClick = onClick),
        cornerRadius = 16.dp
    ) {
        Column {
            Row(verticalAlignment = Alignment.Top) {
                Box(modifier = Modifier.size(46.dp)) {
                    Box(
                        modifier = Modifier
                            .size(46.dp)
                            .clip(RoundedCornerShape(14.dp))
                            .background(if (online) SecondaryContainer else SurfaceContainer),
                        contentAlignment = Alignment.Center
                    ) {
                        Icon(
                            imageVector = Icons.Default.Sensors,
                            contentDescription = null,
                            tint = if (online) PrimaryBlue else Muted,
                            modifier = Modifier.size(22.dp)
                        )
                    }
                    if (online) {
                        Box(
                            modifier = Modifier
                                .align(Alignment.BottomEnd)
                                .size(11.dp)
                                .clip(CircleShape)
                                .background(Success)
                                .graphicsLayer { alpha = pulse }
                        )
                    }
                }
                Spacer(modifier = Modifier.width(12.dp))
                Column(modifier = Modifier.weight(1f)) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Text(
                            text = device.name,
                            style = MaterialTheme.typography.titleMedium.copy(
                                color = OnSurface,
                                fontSize = 17.sp,
                                fontWeight = FontWeight.SemiBold
                            ),
                            maxLines = 1,
                            overflow = TextOverflow.Ellipsis,
                            modifier = Modifier.weight(1f)
                        )
                        Spacer(modifier = Modifier.width(8.dp))
                        Text(
                            text = when (device.status) {
                                "online" -> "ONLINE"
                                "error" -> "ERROR"
                                else -> "OFFLINE"
                            },
                            style = MaterialTheme.typography.labelSmall.copy(
                                color = statusColor,
                                fontWeight = FontWeight.Bold
                            ),
                            modifier = Modifier
                                .clip(RoundedCornerShape(30.dp))
                                .background(statusColor.copy(alpha = 0.12f))
                                .padding(horizontal = 9.dp, vertical = 4.dp)
                        )
                    }
                    Spacer(modifier = Modifier.height(4.dp))
                    Text(
                        text = device.model + " · " +
                            roomLabel(device.roomName, s.devices.unassignedRoom),
                        style = MaterialTheme.typography.labelMedium.copy(color = Muted),
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis
                    )
                    Spacer(modifier = Modifier.height(2.dp))
                    Text(
                        text = device.serialNumber,
                        style = MaterialTheme.typography.labelSmall.copy(color = Hint),
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis
                    )
                }
            }
            HorizontalDivider(
                color = Hairline,
                thickness = 1.dp,
                modifier = Modifier.padding(vertical = 14.dp)
            )
            Row(modifier = Modifier.fillMaxWidth()) {
                Column(modifier = Modifier.weight(1f)) {
                    Text(s.devices.firmware, style = MaterialTheme.typography.labelSmall.copy(color = Hint))
                    Text(
                        text = device.firmwareVersion.ifBlank { "--" },
                        style = MaterialTheme.typography.labelMedium.copy(color = OnSurface)
                    )
                }
                Column(modifier = Modifier.weight(1f)) {
                    Text(s.devices.lastSeen, style = MaterialTheme.typography.labelSmall.copy(color = Hint))
                    Text(
                        text = formatTime(device.lastSeenAt),
                        style = MaterialTheme.typography.labelMedium.copy(color = OnSurface),
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis
                    )
                }
            }
        }
    }
}

@Composable
private fun DeviceDetail(device: Device, onDelete: () -> Unit) {
    val s = com.aura.android.ui.i18n.LocalStrings.current
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 24.dp, vertical = 8.dp)
    ) {
        Text(
            text = device.name,
            style = MaterialTheme.typography.headlineMedium.copy(color = OnSurface)
        )
        Spacer(modifier = Modifier.height(6.dp))
        Text(
            text = "${device.model} · ${device.serialNumber}",
            style = MaterialTheme.typography.bodyMedium.copy(color = Muted)
        )
        Spacer(modifier = Modifier.height(20.dp))
        DetailRow(
            s.devices.status,
            if (device.status == "online") s.common.online else s.common.offline
        )
        DetailRow(s.devices.endpoint, device.endpointUrl?.ifBlank { "--" } ?: "--")
        DetailRow(s.devices.room, roomLabel(device.roomName, s.devices.unassignedRoom))
        DetailRow(s.devices.firmwareVersion, device.firmwareVersion)
        DetailRow(s.devices.lastSeen, formatTime(device.lastSeenAt))
        DetailRow(s.devices.createdAt, formatTime(device.createdAt))
        DetailRow(s.devices.updatedAt, formatTime(device.updatedAt))
        Spacer(modifier = Modifier.height(20.dp))
        TextButton(onClick = onDelete) {
            Text(s.devices.deleteDevice, color = ErrorRed)
        }
        Spacer(modifier = Modifier.height(16.dp))
    }
}

@Composable
private fun DetailRow(label: String, value: String) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(vertical = 10.dp),
        horizontalArrangement = Arrangement.SpaceBetween
    ) {
        Text(label, style = MaterialTheme.typography.bodyMedium.copy(color = Muted))
        Text(
            value,
            style = MaterialTheme.typography.bodyMedium.copy(
                color = OnSurface,
                fontWeight = FontWeight.Medium
            )
        )
    }
    HorizontalDivider(color = Hairline, thickness = 1.dp)
}

private fun roomLabel(raw: String?, fallback: String): String {
    val name = raw?.trim().orEmpty()
    return name.ifBlank { fallback }
}

private fun formatTime(raw: String): String {
    if (raw.isBlank()) return "--"
    return try {
        val instant = java.time.Instant.parse(raw)
        java.time.format.DateTimeFormatter
            .ofPattern("MM-dd HH:mm")
            .withZone(java.time.ZoneId.systemDefault())
            .format(instant)
    } catch (e: Exception) {
        raw.take(16).replace("T", " ")
    }
}

@Composable
private fun DeviceSkeleton(modifier: Modifier = Modifier) {
    val transition = rememberInfiniteTransition(label = "skeleton")
    val alpha by transition.animateFloat(
        initialValue = 0.35f,
        targetValue = 0.8f,
        animationSpec = infiniteRepeatable(
            animation = tween(900),
            repeatMode = RepeatMode.Reverse
        ),
        label = "skeletonAlpha"
    )
    Column(
        modifier = modifier.padding(horizontal = 24.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp)
    ) {
        Spacer(modifier = Modifier.height(8.dp))
        repeat(3) {
            AuraCard {
                Column {
                    Box(
                        modifier = Modifier
                            .fillMaxWidth(0.5f)
                            .height(18.dp)
                            .clip(RoundedCornerShape(6.dp))
                            .background(SurfaceContainer.copy(alpha = alpha))
                    )
                    Spacer(modifier = Modifier.height(10.dp))
                    Box(
                        modifier = Modifier
                            .fillMaxWidth()
                            .height(12.dp)
                            .clip(RoundedCornerShape(6.dp))
                            .background(SurfaceContainer.copy(alpha = alpha))
                    )
                    Spacer(modifier = Modifier.height(8.dp))
                    Box(
                        modifier = Modifier
                            .fillMaxWidth(0.7f)
                            .height(12.dp)
                            .clip(RoundedCornerShape(6.dp))
                            .background(SurfaceContainer.copy(alpha = alpha))
                    )
                }
            }
        }
    }
}

@Composable
private fun EmptyDevices(onRefresh: () -> Unit) {
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .padding(vertical = 48.dp),
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        Canvas(modifier = Modifier.size(96.dp)) {
            drawCircle(
                color = PrimaryBlue.copy(alpha = 0.08f),
                radius = size.minDimension / 2f
            )
            drawCircle(
                color = PrimaryBlue.copy(alpha = 0.18f),
                radius = size.minDimension / 2.6f,
                style = Stroke(width = 1.5.dp.toPx())
            )
            val path = Path().apply {
                moveTo(size.width * 0.3f, size.height * 0.55f)
                lineTo(size.width * 0.45f, size.height * 0.42f)
                lineTo(size.width * 0.58f, size.height * 0.5f)
                lineTo(size.width * 0.72f, size.height * 0.34f)
            }
            drawPath(path, color = PrimaryBlue, style = Stroke(width = 2.dp.toPx()))
        }
        Spacer(modifier = Modifier.height(16.dp))
        val s = com.aura.android.ui.i18n.LocalStrings.current
        Text(
            text = s.devices.empty,
            style = MaterialTheme.typography.titleLarge.copy(color = OnSurface)
        )
        Spacer(modifier = Modifier.height(6.dp))
        Text(
            text = s.devices.emptyHint,
            style = MaterialTheme.typography.bodyMedium.copy(color = Muted)
        )
        Spacer(modifier = Modifier.height(12.dp))
        TextButton(onClick = onRefresh) {
            Text(s.devices.resync, color = PrimaryBlue)
        }
    }
}

@Composable
private fun DeviceError(message: String, onRetry: () -> Unit, modifier: Modifier = Modifier) {
    val s = com.aura.android.ui.i18n.LocalStrings.current
    Column(
        modifier = modifier,
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center
    ) {
        Text(message, color = ErrorRed, style = MaterialTheme.typography.bodyLarge)
        Spacer(modifier = Modifier.height(12.dp))
        TextButton(onClick = onRetry) {
            Text(s.common.retry, color = PrimaryBlue)
        }
    }
}
