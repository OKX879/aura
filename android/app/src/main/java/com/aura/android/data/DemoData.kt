package com.aura.android.data

import com.aura.android.data.model.Alert
import com.aura.android.data.model.AlertSummary
import com.aura.android.data.model.Device
import com.aura.android.data.model.DeviceSummary
import com.aura.android.data.model.FocusRecord
import com.aura.android.data.model.MobileDashboard
import com.aura.android.data.model.TodaySummary
import com.aura.android.data.model.TrendPoint
import com.aura.android.data.model.User
import com.aura.android.data.model.VitalsPoint
import java.time.Instant
import java.time.temporal.ChronoUnit
import kotlin.math.sin

// 演示数据
object DemoData {

    private fun iso(minutesAgo: Long): String =
        Instant.now().minus(minutesAgo, ChronoUnit.MINUTES).toString()

    val user = User(
        id = "demo-user",
        name = "演示用户",
        employeeId = "A-0001",
        role = "member",
        email = "demo@aura.local",
        status = "active",
        createdAt = iso(60 * 24 * 30),
        updatedAt = iso(5)
    )

    val latestRecord = FocusRecord(
        id = "demo-record",
        deviceId = "demo-device",
        focusScore = 82.0,
        stressLevel = 24.0,
        fatigueLevel = 18.0,
        heartRate = 68.0,
        respirationRate = 16.0,
        nnHeartRate = 67.0,
        cHeartRate = 69.0,
        ppiHeartRate = 68.0,
        medianPpiMs = 882.0,
        cRespirationRate = 16.2,
        nnQuality = 93.0,
        recordedAt = iso(2)
    )

    val todaySummary = TodaySummary(
        averageFocus = 78.0,
        maxFocus = 92.0,
        minFocus = 61.0,
        recordCount = 148,
        averageHeartRate = 69.0,
        averageRespirationRate = 16.0,
        averageStress = 26.0,
        averageFatigue = 21.0,
        maxHeartRate = 84.0,
        minHeartRate = 58.0
    )

    // 今日趋势数据
    val recentTrend: List<TrendPoint> = (0 until 24).map { i ->
        val base = 62.0 + 18.0 * sin(i / 24.0 * Math.PI * 1.6)
        val dip = if (i in 12..14) 9.0 else 0.0
        TrendPoint(
            time = iso((23 - i).toLong() * 30),
            averageFocus = (base - dip).coerceIn(40.0, 96.0),
            peopleCount = 1
        )
    }

    // 体征历史数据
    val vitalsHistory: List<VitalsPoint> = (0 until 12).map { i ->
        val phase = i / 12.0 * Math.PI * 2
        VitalsPoint(
            time = iso((11 - i).toLong() * 10),
            focusScore = 74.0 + 10.0 * sin(phase),
            heartRate = 68.0 + 6.0 * sin(phase + 0.4),
            respirationRate = 16.0 + 2.4 * sin(phase + 0.9),
            stressLevel = 27.0 + 8.0 * sin(phase + 1.6),
            fatigueLevel = 20.0 + 6.0 * sin(phase + 2.2)
        )
    }

    // 演示设备列表
    val devices = listOf(
        Device(
            id = "demo-device",
            name = "AURA 雷达 · 书房",
            serialNumber = "RS6240-P2512-0417",
            model = "RS6240-P2512",
            status = "online",
            firmwareVersion = "2.1.2",
            lastSeenAt = iso(1),
            createdAt = iso(60 * 24 * 90),
            updatedAt = iso(1)
        ),
        Device(
            id = "demo-device-2",
            name = "AURA 雷达 · 卧室",
            serialNumber = "RS6240-P2512-0418",
            model = "RS6240-P2512",
            status = "online",
            firmwareVersion = "2.1.2",
            lastSeenAt = iso(3),
            createdAt = iso(60 * 24 * 60),
            updatedAt = iso(3)
        ),
        Device(
            id = "demo-device-3",
            name = "AURA 雷达 · 客厅",
            serialNumber = "RS6240-P2512-0419",
            model = "RS6240-P2512",
            status = "offline",
            firmwareVersion = "2.1.1",
            lastSeenAt = iso(60 * 26),
            createdAt = iso(60 * 24 * 45),
            updatedAt = iso(60 * 26)
        )
    )

    val alerts = listOf(
        Alert(
            id = "demo-alert-1",
            type = "vital_abnormal",
            severity = "warning",
            deviceId = "demo-device",
            message = "静息心率连续 5 分钟高于个人基线 12 BPM",
            isResolved = false,
            createdAt = iso(26)
        ),
        Alert(
            id = "demo-alert-2",
            type = "device_offline",
            severity = "info",
            deviceId = "demo-device-3",
            message = "客厅设备已离线 26 小时",
            isResolved = false,
            createdAt = iso(60 * 26)
        )
    )

    val dashboard = MobileDashboard(
        user = user,
        latestRecord = latestRecord,
        todaySummary = todaySummary,
        recentTrend = recentTrend,
        vitalsHistory = vitalsHistory,
        alertSummary = AlertSummary(
            unresolvedCount = alerts.count { !it.isResolved },
            recent = alerts
        ),
        deviceSummary = DeviceSummary(
            online = devices.count { it.status == "online" },
            total = devices.size,
            recent = devices
        )
    )
}
