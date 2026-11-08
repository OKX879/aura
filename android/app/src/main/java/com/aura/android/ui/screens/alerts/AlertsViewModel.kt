package com.aura.android.ui.screens.alerts

import androidx.lifecycle.ViewModel
import com.aura.android.data.AlertHistory
import com.aura.android.data.LiveMonitor
import com.aura.android.data.model.Alert
import kotlinx.coroutines.flow.StateFlow

// 告警页状态
class AlertsViewModel : ViewModel() {

    val alerts: StateFlow<List<Alert>> = LiveMonitor.alerts

    val history: StateFlow<List<Alert>> = AlertHistory.items

    val online: StateFlow<Boolean> = LiveMonitor.online

    init {
        LiveMonitor.ensureStarted()
    }

    fun refresh() {
        // 确保实时监测已启动
        LiveMonitor.ensureStarted()
    }

    // 忽略一条实时告警
    fun dismiss(type: String) {
        LiveMonitor.dismiss(type)
    }

    // 删除历史告警
    fun removeHistory(alert: Alert) {
        AlertHistory.remove(alert)
    }

    // 清空历史告警
    fun clearHistory() {
        AlertHistory.clear()
    }
}
