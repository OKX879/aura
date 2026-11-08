package com.aura.android.ui.screens.devices

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.aura.android.data.DeviceRepository
import com.aura.android.data.DiscoveredDevice
import com.aura.android.data.LocalMode
import com.aura.android.data.LocalStore
import com.aura.android.data.PairingClient
import com.aura.android.data.api.ApiClient
import com.aura.android.data.api.ApiResult
import com.aura.android.data.api.toApiResult
import com.aura.android.data.model.Device
import kotlinx.coroutines.launch

class DevicesViewModel : ViewModel() {

    var uiState by mutableStateOf<ApiResult<List<Device>>>(ApiResult.Loading)
        private set

    // 局域网扫描状态与结果
    var scanning by mutableStateOf(false)
        private set

    var discovered by mutableStateOf<List<DiscoveredDevice>>(emptyList())
        private set

    init {
        loadDevices()
    }

    fun loadDevices(silent: Boolean = false) {
        if (LocalMode.ENABLED) {
            // 共用本地设备列表
            uiState = ApiResult.Success(LocalStore.devices.toList())
            return
        }
        viewModelScope.launch {
            if (!silent) {
                uiState = ApiResult.Loading
            }
            try {
                val response = ApiClient.auraApi.getDevices()
                uiState = response.toApiResult()
            } catch (e: Exception) {
                uiState = ApiResult.Error(e.message ?: "加载失败")
            }
        }
    }

    // 添加设备并刷新列表
    suspend fun addDevice(name: String, endpoint: String, serial: String): Result<Device> {
        val result = DeviceRepository.add(name, endpoint, serial)
        if (result.isSuccess) loadDevices()
        return result
    }

    // 扫描局域网设备
    fun scanNearby() {
        if (scanning) return
        viewModelScope.launch {
            scanning = true
            try {
                discovered = PairingClient.discover()
            } finally {
                scanning = false
            }
        }
    }

    // 配对验证后注册设备
    suspend fun pairAndAdd(
        name: String,
        endpoint: String,
        code: String
    ): Result<Device> {
        val result = PairingClient.verify(endpoint, code).mapCatching { info ->
            DeviceRepository.add(
                name = name.ifBlank { info.name.ifBlank { "AURA 雷达" } },
                endpointUrl = endpoint,
                serialNumber = info.serial
            ).getOrThrow()
        }
        if (result.isSuccess) loadDevices()
        return result
    }

    fun deleteDevice(id: String) {
        viewModelScope.launch {
            DeviceRepository.remove(id)
            loadDevices()
        }
    }
}
