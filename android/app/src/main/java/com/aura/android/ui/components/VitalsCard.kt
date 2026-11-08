package com.aura.android.ui.components

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.aura.android.ui.theme.Hint
import com.aura.android.ui.theme.OnSurface

// 指标卡片
@Composable
fun VitalsCard(
    icon: ImageVector,
    iconColor: Color,
    label: String,
    value: String,
    unit: String,
    status: String? = null,
    statusColor: Color? = null,
    modifier: Modifier = Modifier
) {
    AuraCard(modifier = modifier) {
        Column {
            Row(
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(6.dp)
            ) {
                Icon(
                    imageVector = icon,
                    contentDescription = null,
                    tint = iconColor,
                    modifier = Modifier.size(14.dp)
                )
                Text(
                    text = label,
                    style = MaterialTheme.typography.labelSmall.copy(color = Hint)
                )
            }
            Spacer(modifier = Modifier.height(10.dp))
            Row(verticalAlignment = Alignment.Bottom) {
                Text(
                    text = value,
                    style = MaterialTheme.typography.headlineLarge.copy(
                        fontWeight = FontWeight.Bold,
                        color = OnSurface
                    )
                )
                Text(
                    text = unit,
                    style = MaterialTheme.typography.labelMedium.copy(color = Hint),
                    modifier = Modifier.padding(bottom = 3.dp, start = 4.dp)
                )
            }
            if (status != null && statusColor != null) {
                Spacer(modifier = Modifier.height(10.dp))
                Text(
                    text = status,
                    style = MaterialTheme.typography.labelSmall.copy(
                        color = statusColor,
                        fontWeight = FontWeight.SemiBold
                    ),
                    modifier = Modifier
                        .clip(RoundedCornerShape(30.dp))
                        .background(statusColor.copy(alpha = 0.12f))
                        .padding(horizontal = 10.dp, vertical = 4.dp)
                )
            }
        }
    }
}
