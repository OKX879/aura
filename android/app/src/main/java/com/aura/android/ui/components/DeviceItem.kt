package com.aura.android.ui.components

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.Sensors
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.aura.android.data.model.Device
import com.aura.android.ui.theme.Hint
import com.aura.android.ui.theme.LabelSecondary
import com.aura.android.ui.theme.LabelTertiary
import com.aura.android.ui.theme.SuccessGreen

// 设备列表项，支持删除
@Composable
fun DeviceItem(device: Device, onDelete: (() -> Unit)? = null) {
    val statusColor = when (device.status) {
        "online" -> SuccessGreen
        "error" -> Color(0xFFB4472B)
        else -> LabelTertiary
    }

    AuraCard(modifier = Modifier.fillMaxWidth()) {
        Row(
            verticalAlignment = Alignment.CenterVertically,
            modifier = Modifier.fillMaxWidth()
        ) {
            Icon(
                imageVector = Icons.Default.Sensors,
                contentDescription = device.status,
                tint = MaterialTheme.colorScheme.primary,
                modifier = Modifier.size(28.dp)
            )
            Spacer(modifier = Modifier.width(14.dp))
            Column(modifier = Modifier.weight(1f)) {
                Text(
                    text = device.name,
                    style = MaterialTheme.typography.titleMedium.copy(
                        fontWeight = FontWeight.SemiBold
                    ),
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis
                )
                Spacer(modifier = Modifier.height(3.dp))
                Text(
                    text = "${device.model} · ${device.serialNumber}",
                    style = MaterialTheme.typography.labelSmall,
                    color = LabelSecondary,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis
                )
                Spacer(modifier = Modifier.height(3.dp))
                Text(
                    text = "固件 ${device.firmwareVersion}",
                    style = MaterialTheme.typography.labelSmall,
                    color = LabelTertiary,
                    maxLines = 1
                )
            }
            Spacer(modifier = Modifier.width(12.dp))
            Column(
                horizontalAlignment = Alignment.End,
                modifier = Modifier.widthIn(max = 96.dp)
            ) {
                Text(
                    text = if (device.status == "online") "在线" else "离线",
                    style = MaterialTheme.typography.labelMedium.copy(
                        fontWeight = FontWeight.SemiBold,
                        color = statusColor
                    ),
                    maxLines = 1
                )
                Spacer(modifier = Modifier.height(3.dp))
                Text(
                    text = formatSeen(device.lastSeenAt),
                    style = MaterialTheme.typography.labelSmall,
                    color = LabelTertiary,
                    maxLines = 1
                )
            }
            if (onDelete != null) {
                Spacer(modifier = Modifier.width(8.dp))
                Box(
                    modifier = Modifier
                        .size(26.dp)
                        .clip(CircleShape)
                        .clickable(onClick = onDelete),
                    contentAlignment = Alignment.Center
                ) {
                    Icon(
                        imageVector = Icons.Default.Close,
                        contentDescription = "删除设备",
                        tint = Hint,
                        modifier = Modifier.size(15.dp)
                    )
                }
            }
        }
    }
}

// 格式化时间为 MM-dd HH:mm
private fun formatSeen(raw: String): String {
    if (raw.isBlank()) return "--"
    return try {
        java.time.format.DateTimeFormatter
            .ofPattern("MM-dd HH:mm")
            .withZone(java.time.ZoneId.systemDefault())
            .format(java.time.Instant.parse(raw))
    } catch (e: Exception) {
        raw.take(10)
    }
}
