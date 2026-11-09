package com.aura.android.data

import com.aura.android.data.api.ApiClient
import com.aura.android.data.model.ApiResponse
import com.aura.android.data.model.CreateDeviceRequest
import com.aura.android.data.model.Device
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonElement
import retrofit2.HttpException

// 设备增删操作
object DeviceRepository {

    private val json = Json { ignoreUnknownKeys = true }

    suspend fun add(name: String, endpointUrl: String, serialNumber: String): Result<Device> {
        if (LocalMode.ENABLED) {
            return Result.success(LocalStore.addDevice(name, serialNumber))
        }
        val attempt = runCatching {
            val response = ApiClient.auraApi.createDevice(
                CreateDeviceRequest(
                    name = name.trim(),
                    endpointUrl = endpointUrl.trim().ifBlank { null },
                    serialNumber = serialNumber.trim().ifBlank { null }
                )
            )
            response.data ?: throw IllegalStateException(
                response.error?.message ?: "注册失败，请检查服务地址与登录状态"
            )
        }
        return attempt.fold(
            onSuccess = { Result.success(it) },
            onFailure = { error ->
                if (error is HttpException) {
                    Result.failure(
                        IllegalStateException(serverMessage(error, "注册失败"), error)
                    )
                } else {
                    Result.failure(error)
                }
            }
        )
    }

    suspend fun remove(id: String): Result<Unit> {
        if (LocalMode.ENABLED) {
            LocalStore.removeDevice(id)
            return Result.success(Unit)
        }
        return runCatching {
            val response = ApiClient.auraApi.deleteDevice(id)
            if (!response.success) {
                throw IllegalStateException(response.error?.message ?: "删除失败")
            }
        }
    }

    // 提取服务端错误信息
    private fun serverMessage(error: HttpException, fallback: String): String {
        val body = runCatching { error.response()?.errorBody()?.string() }.getOrNull()
        if (!body.isNullOrBlank()) {
            val parsed = runCatching {
                json.decodeFromString<ApiResponse<JsonElement>>(body)
            }.getOrNull()
            parsed?.error?.message?.let { return it }
        }
        return "$fallback (HTTP ${error.code()})"
    }
}
