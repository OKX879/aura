package com.aura.android.data.api

import com.aura.android.data.model.ApiError

// API 调用结果封装
sealed class ApiResult<out T> {
    data object Loading : ApiResult<Nothing>()
    data class Success<T>(val data: T) : ApiResult<T>()
    data class Error(val message: String, val code: String? = null) : ApiResult<Nothing>()
}

// 转换为 ApiResult
fun <T> com.aura.android.data.model.ApiResponse<T>.toApiResult(): ApiResult<T> {
    return if (success && data != null) {
        ApiResult.Success(data)
    } else {
        ApiResult.Error(
            message = error?.message ?: "未知错误",
            code = error?.code
        )
    }
}
