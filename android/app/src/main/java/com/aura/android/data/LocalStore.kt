package com.aura.android.data

import androidx.compose.runtime.mutableStateListOf
import com.aura.android.data.model.Alert
import com.aura.android.data.model.Device
import java.time.Instant

// 本地模式可变数据源
object LocalStore {

    val devices = mutableStateListOf<Device>().apply { addAll(DemoData.devices) }

    val alerts = mutableStateListOf<Alert>().apply { addAll(DemoData.alerts) }

    private var serialSeed = 100

    fun addDevice(name: String, serial: String): Device {
        val now = Instant.now().toString()
        serialSeed++
        val device = Device(
            id = "local-device-${System.currentTimeMillis()}",
            name = name.trim().ifBlank { "AURA 雷达 · 书房" },
            serialNumber = serial.trim().ifBlank { "RS6240-P2512-0$serialSeed" },
            model = "RS6240-P2512",
            status = "online",
            firmwareVersion = "2.1.2",
            lastSeenAt = now,
            createdAt = now,
            updatedAt = now
        )
        devices.add(0, device)
        return device
    }

    fun removeDevice(id: String) {
        devices.removeAll { it.id == id }
    }

    fun onlineCount(): Int = devices.count { it.status == "online" }

    fun unresolvedAlertCount(): Int = alerts.count { !it.isResolved }

    // 最近告警排序
    fun recentAlerts(limit: Int = 3): List<Alert> = alerts
        .sortedWith(compareBy<Alert> { it.isResolved }.thenByDescending { it.createdAt })
        .take(limit)

    // 最近设备排序
    fun recentDevices(limit: Int = 3): List<Device> = devices
        .sortedWith(compareByDescending<Device> { it.status == "online" }.thenByDescending { it.lastSeenAt })
        .take(limit)
}
