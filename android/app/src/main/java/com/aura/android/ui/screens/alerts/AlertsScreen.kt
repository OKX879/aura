package com.aura.android.ui.screens.alerts

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
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.aura.android.ui.components.AlertItem
import com.aura.android.ui.theme.AppleBlue
import com.aura.android.ui.theme.Hint
import com.aura.android.ui.theme.Ink
import com.aura.android.ui.theme.LabelSecondary
import com.aura.android.ui.theme.LabelTertiary
import com.aura.android.ui.theme.Success

// 告警列表页
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun AlertsScreen(viewModel: AlertsViewModel) {
    val alerts by viewModel.alerts.collectAsState()
    val history by viewModel.history.collectAsState()
    val online by viewModel.online.collectAsState()
    val s = com.aura.android.ui.i18n.LocalStrings.current

    Scaffold(
        containerColor = androidx.compose.ui.graphics.Color.Transparent,
        topBar = {
            TopAppBar(
                title = {
                    Text(
                        text = s.alerts.title,
                        style = MaterialTheme.typography.titleLarge.copy(
                            fontWeight = FontWeight.SemiBold
                        )
                    )
                },
                actions = {
                    IconButton(onClick = viewModel::refresh) {
                        Icon(Icons.Default.Refresh, contentDescription = s.common.refresh)
                    }
                },
                colors = TopAppBarDefaults.topAppBarColors(
                    containerColor = androidx.compose.ui.graphics.Color.Transparent
                )
            )
        }
    ) { padding ->
        var appeared by remember { mutableStateOf(false) }
        LaunchedEffect(Unit) { appeared = true }
        val alpha by animateFloatAsState(
            targetValue = if (appeared) 1f else 0f,
            animationSpec = tween(350),
            label = "alertsAlpha"
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
                Column(modifier = Modifier.padding(bottom = 8.dp)) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Text(
                            text = "ALERTS",
                            style = MaterialTheme.typography.labelSmall.copy(color = LabelTertiary)
                        )
                        Spacer(modifier = Modifier.size(8.dp))
                        Box(
                            modifier = Modifier
                                .size(6.dp)
                                .clip(CircleShape)
                                .background(if (online) Success else Hint)
                        )
                        Spacer(modifier = Modifier.size(5.dp))
                        Text(
                            text = if (online) s.alerts.monitoring else s.alerts.deviceOffline,
                            style = MaterialTheme.typography.labelSmall.copy(
                                color = if (online) Success else Hint
                            )
                        )
                    }
                    Spacer(modifier = Modifier.height(6.dp))
                    Text(
                        text = s.alerts.liveAlerts,
                        style = MaterialTheme.typography.headlineMedium.copy(color = Ink)
                    )
                    Spacer(modifier = Modifier.height(4.dp))
                    Text(
                        text = if (alerts.isEmpty()) {
                            s.alerts.noAnomaly
                        } else {
                            String.format(s.alerts.pending, alerts.size)
                        },
                        style = MaterialTheme.typography.bodyMedium.copy(color = LabelSecondary)
                    )
                }
            }
            if (alerts.isEmpty()) {
                item {
                    Column(
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(vertical = 48.dp),
                        horizontalAlignment = Alignment.CenterHorizontally
                    ) {
                        Text(
                            text = s.alerts.none,
                            style = MaterialTheme.typography.titleLarge.copy(color = Ink)
                        )
                        Spacer(modifier = Modifier.height(6.dp))
                        Text(
                            text = s.alerts.noneHint,
                            style = MaterialTheme.typography.bodyMedium.copy(color = LabelSecondary)
                        )
                    }
                }
            } else {
                items(alerts, key = { it.id }) { alert ->
                    AlertItem(
                        alert = alert,
                        onDelete = { viewModel.dismiss(alert.type) }
                    )
                }
            }
            if (history.isNotEmpty()) {
                item {
                    Row(
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(top = 20.dp, bottom = 4.dp),
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Column(modifier = Modifier.weight(1f)) {
                            Text(
                                text = s.alerts.history,
                                style = MaterialTheme.typography.titleMedium.copy(
                                    fontWeight = FontWeight.SemiBold,
                                    color = Ink
                                )
                            )
                            Spacer(modifier = Modifier.height(4.dp))
                            Text(
                                text = String.format(s.alerts.historyHint, history.size),
                                style = MaterialTheme.typography.bodySmall.copy(color = LabelSecondary)
                            )
                        }
                        Text(
                            text = s.common.clearAll,
                            style = MaterialTheme.typography.labelSmall.copy(color = LabelTertiary),
                            modifier = Modifier
                                .clip(RoundedCornerShape(30.dp))
                                .clickable(onClick = viewModel::clearHistory)
                                .padding(horizontal = 10.dp, vertical = 5.dp)
                        )
                    }
                }
                items(history, key = { "h-${it.type}-${it.createdAt}" }) { alert ->
                    AlertItem(
                        alert = alert,
                        onDelete = { viewModel.removeHistory(alert) }
                    )
                }
            }
            item { Spacer(modifier = Modifier.height(96.dp)) }
        }
    }
}
