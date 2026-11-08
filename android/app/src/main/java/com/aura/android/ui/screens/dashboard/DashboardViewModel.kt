package com.aura.android.ui.screens.dashboard

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.aura.android.data.DemoData
import com.aura.android.data.LiveMonitor
import com.aura.android.data.LocalMode
import com.aura.android.data.api.ApiClient
import com.aura.android.data.api.ApiResult
import com.aura.android.data.api.toApiResult
import com.aura.android.data.model.MobileDashboard
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch

class DashboardViewModel : ViewModel() {

    var uiState by mutableStateOf<ApiResult<MobileDashboard>>(ApiResult.Loading)
        private set

    var lastUpdatedAt by mutableStateOf<Long?>(null)
        private set

    init {
        if (LocalMode.ENABLED) {
            // 本地模式使用演示数据
            uiState = ApiResult.Success(DemoData.dashboard)
            lastUpdatedAt = System.currentTimeMillis()
        } else {
            LiveMonitor.ensureStarted()
            loadDashboard()
            viewModelScope.launch {
                while (isActive) {
                    delay(5_000)
                    loadDashboard(showLoading = false)
                }
            }
        }
    }

    fun loadDashboard(showLoading: Boolean = true) {
        viewModelScope.launch {
            if (showLoading && uiState !is ApiResult.Success) {
                uiState = ApiResult.Loading
            }
            try {
                val response = ApiClient.auraApi.getDashboard()
                uiState = response.toApiResult()
                if (uiState is ApiResult.Success) {
                    lastUpdatedAt = System.currentTimeMillis()
                }
            } catch (e: Exception) {
                // 请求失败时直接报错
                uiState = ApiResult.Error(e.message ?: "无法连接服务器")
            }
        }
    }
}
