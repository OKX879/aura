package com.aura.android.data.model

import kotlinx.serialization.SerialName
import kotlinx.serialization.Serializable

// 后端统一响应包装
@Serializable
data class ApiResponse<T>(
    val success: Boolean,
    val data: T? = null,
    val error: ApiError? = null
)

@Serializable
data class ApiError(
    val message: String,
    val code: String? = null
)

// 用户
@Serializable
data class User(
    val id: String,
    val name: String,
    val employeeId: String,
    val departmentId: String? = null,
    val role: String,
    val email: String,
    val phone: String? = null,
    val status: String,
    val createdAt: String,
    val updatedAt: String,
    // 头像 Base64 字符串
    val avatar: String? = null
)

// 修改昵称与头像
@Serializable
data class UpdateProfileRequest(
    val name: String? = null,
    val avatar: String? = null
)

// 专注记录
@Serializable
data class FocusRecord(
    val id: String,
    val userId: String? = null,
    val deviceId: String,
    val roomId: String? = null,
    val focusScore: Double,
    val stressLevel: Double? = null,
    val fatigueLevel: Double? = null,
    val heartRate: Double? = null,
    val respirationRate: Double? = null,
    @SerialName("nnHeartRate") val nnHeartRate: Double? = null,
    @SerialName("cHeartRate") val cHeartRate: Double? = null,
    @SerialName("ppiHeartRate") val ppiHeartRate: Double? = null,
    @SerialName("medianPpiMs") val medianPpiMs: Double? = null,
    @SerialName("cRespirationRate") val cRespirationRate: Double? = null,
    @SerialName("nnQuality") val nnQuality: Double? = null,
    val recordedAt: String
)

// 设备
@Serializable
data class Device(
    val id: String,
    val name: String,
    val serialNumber: String,
    val model: String,
    val roomId: String? = null,
    // 房间名称
    val roomName: String? = null,
    val status: String,
    val firmwareVersion: String,
    val lastSeenAt: String = "",
    // 设备访问地址
    val endpointUrl: String? = null,
    val createdAt: String,
    val updatedAt: String
)

// 添加设备请求
@Serializable
data class CreateDeviceRequest(
    val name: String,
    val endpointUrl: String? = null,
    val serialNumber: String? = null,
    val model: String? = null
)

@Serializable
data class DeleteResult(val deleted: Boolean = false)

// 告警
@Serializable
data class Alert(
    val id: String,
    val type: String,
    val severity: String,
    val userId: String? = null,
    val deviceId: String? = null,
    val roomId: String? = null,
    val message: String,
    val isResolved: Boolean,
    val resolvedAt: String? = null,
    val createdAt: String
)

// 今日汇总
@Serializable
data class TodaySummary(
    val averageFocus: Double,
    val maxFocus: Double,
    val minFocus: Double,
    val recordCount: Int,
    val averageHeartRate: Double? = null,
    val averageRespirationRate: Double? = null,
    val averageStress: Double? = null,
    val averageFatigue: Double? = null,
    val maxHeartRate: Double? = null,
    val minHeartRate: Double? = null
)

// 趋势点
@Serializable
data class TrendPoint(
    val time: String,
    val averageFocus: Double,
    val peopleCount: Int
)

// 生命体征历史点
@Serializable
data class VitalsPoint(
    val time: String,
    val focusScore: Double,
    val heartRate: Double? = null,
    val respirationRate: Double? = null,
    val stressLevel: Double? = null,
    val fatigueLevel: Double? = null
)

// 告警汇总
@Serializable
data class AlertSummary(
    val unresolvedCount: Int,
    val recent: List<Alert>
)

// 设备汇总
@Serializable
data class DeviceSummary(
    val online: Int,
    val total: Int,
    val recent: List<Device>
)

// 移动端仪表盘
@Serializable
data class MobileDashboard(
    val user: User,
    val latestRecord: FocusRecord? = null,
    val todaySummary: TodaySummary,
    val recentTrend: List<TrendPoint>,
    val vitalsHistory: List<VitalsPoint> = emptyList(),
    val alertSummary: AlertSummary,
    val deviceSummary: DeviceSummary
)

// 登录请求与响应
@Serializable
data class LoginRequest(
    val email: String,
    val password: String,
    val pairCode: String
)

@Serializable
data class LoginResponse(
    val token: String,
    val user: User
)

@Serializable
data class SignupRequest(
    val name: String,
    val email: String,
    val password: String,
    val pairCode: String
)

// AI 对话
@Serializable
data class ChatMessage(
    val role: String,
    val content: String
)

@Serializable
data class ChatRequest(
    val message: String,
    val history: List<ChatMessage> = emptyList()
)

@Serializable
data class ChatResponse(
    val reply: String
)

// 实时雷达快照
@Serializable
data class LiveSnapshot(
    val source: String = "",
    val spidev: String = "",
    val uart: String = "",
    @SerialName("spi_hz") val spiHz: Long = 0,
    val running: Boolean = false,
    @SerialName("started_at_ms") val startedAtMs: Long = 0,
    val frames: Long = 0,
    val lost: Long = 0,
    val errors: Long = 0,
    val channels: Int = 0,
    val bins: Int = 0,
    val fs: Double = 0.0,
    val latest: LiveLatest? = null
)

// 单帧算法输出
@Serializable
data class LiveLatest(
    val t: Long = 0,
    val frame: Long = 0,
    @SerialName("radar_frame") val radarFrame: Long = 0,
    val reads: Int = 0,
    val overrun: Long = 0,
    val hr: Double? = null,
    @SerialName("hr_fft") val hrFft: Double? = null,
    @SerialName("hr_acf") val hrAcf: Double? = null,
    @SerialName("hr_ppi") val hrPpi: Double? = null,
    @SerialName("hr_fused") val hrFused: Double? = null,
    val conf: Double? = null,
    val confident: Int = 0,
    val consistent: Int = 0,
    @SerialName("resp_hz") val respHz: Double? = null,
    @SerialName("ppi_source") val ppiSource: Int = 0,
    @SerialName("n_beats") val nBeats: Int = 0,
    val bin: Int = 0,
    val presence: Int = 0,
    val motion: Int = 0,
    val amp: Double? = null,
    val coh: Double? = null,
    @SerialName("motion_level") val motionLevel: Double? = null,
    @SerialName("motion_excursion") val motionExcursion: Double? = null
)
