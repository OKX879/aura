package com.aura.android.ui.components

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.ErrorOutline
import androidx.compose.material.icons.filled.Info
import androidx.compose.material.icons.filled.Warning
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.aura.android.data.model.Alert
import com.aura.android.ui.theme.DangerRed
import com.aura.android.ui.theme.LabelTertiary
import com.aura.android.ui.theme.WarningOrange
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

// 告警列表项，支持删除
@Composable
fun AlertItem(alert: Alert, onDelete: (() -> Unit)? = null) {
    val (icon, color) = when (alert.severity) {
        "high" -> Icons.Default.ErrorOutline to DangerRed
        "medium" -> Icons.Default.Warning to WarningOrange
        else -> Icons.Default.Info to LabelTertiary
    }
    val severityLabel = when (alert.severity) {
        "high" -> "高"
        "medium" -> "中"
        else -> "低"
    }

    AuraCard(modifier = Modifier.fillMaxWidth()) {
        Row(
            verticalAlignment = Alignment.CenterVertically,
            modifier = Modifier.fillMaxWidth()
        ) {
            Icon(
                imageVector = icon,
                contentDescription = severityLabel,
                tint = color,
                modifier = Modifier.size(28.dp)
            )
            Spacer(modifier = Modifier.width(12.dp))
            Column(modifier = Modifier.weight(1f)) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text(
                        text = typeLabel(alert.type),
                        style = MaterialTheme.typography.labelMedium.copy(
                            fontWeight = FontWeight.SemiBold,
                            color = color
                        )
                    )
                    Spacer(modifier = Modifier.width(6.dp))
                    Text(
                        text = severityLabel,
                        style = MaterialTheme.typography.labelSmall.copy(
                            color = color,
                            fontWeight = FontWeight.Bold
                        ),
                        modifier = Modifier
                            .clip(RoundedCornerShape(30.dp))
                            .background(color.copy(alpha = 0.12f))
                            .padding(horizontal = 6.dp, vertical = 1.dp)
                    )
                }
                Spacer(modifier = Modifier.height(2.dp))
                Text(
                    text = alert.message,
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurface
                )
                Spacer(modifier = Modifier.height(4.dp))
                Text(
                    text = displayTime(alert.createdAt),
                    style = MaterialTheme.typography.labelSmall,
                    color = LabelTertiary
                )
            }
            if (onDelete != null) {
                IconButton(
                    onClick = onDelete,
                    modifier = Modifier.size(32.dp)
                ) {
                    Icon(
                        imageVector = Icons.Default.Close,
                        contentDescription = "删除",
                        tint = LabelTertiary,
                        modifier = Modifier.size(18.dp)
                    )
                }
            }
        }
    }
}

private fun typeLabel(type: String): String = when (type) {
    "device_offline" -> "设备离线"
    "radar_stalled" -> "数据中断"
    "no_presence" -> "无人值守"
    "hr_high" -> "心率过高"
    "hr_low" -> "心率过低"
    "hr_elevated" -> "心率偏高"
    "motion" -> "体动干扰"
    "weak_signal" -> "信号偏弱"
    "fatigue" -> "疲劳上升"
    "low_focus" -> "专注度下降"
    "high_stress" -> "压力偏高"
    else -> type.replace("_", " ").uppercase()
}

private fun displayTime(raw: String): String {
    val parsed = try {
        SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.getDefault()).parse(raw)
    } catch (e: Exception) {
        null
    } ?: return raw.take(16).replace("T", " ")
    val now = Date()
    val sameDay = SimpleDateFormat("yyyyMMdd", Locale.getDefault())
        .format(now) == SimpleDateFormat("yyyyMMdd", Locale.getDefault()).format(parsed)
    return SimpleDateFormat(if (sameDay) "HH:mm:ss" else "MM-dd HH:mm", Locale.getDefault())
        .format(parsed)
}
