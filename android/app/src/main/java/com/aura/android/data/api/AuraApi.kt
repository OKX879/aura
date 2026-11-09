package com.aura.android.data.api

import com.aura.android.data.model.Alert
import com.aura.android.data.model.ApiResponse
import com.aura.android.data.model.ChatRequest
import com.aura.android.data.model.ChatResponse
import com.aura.android.data.model.CreateDeviceRequest
import com.aura.android.data.model.DeleteResult
import com.aura.android.data.model.Device
import com.aura.android.data.model.LiveSnapshot
import com.aura.android.data.model.LoginRequest
import com.aura.android.data.model.LoginResponse
import com.aura.android.data.model.SignupRequest
import com.aura.android.data.model.MobileDashboard
import com.aura.android.data.model.UpdateProfileRequest
import com.aura.android.data.model.User
import retrofit2.http.Body
import retrofit2.http.DELETE
import retrofit2.http.GET
import retrofit2.http.POST
import retrofit2.http.Path

// 后端 API 接口定义
interface AuraApi {

    @POST("api/v1/auth/login")
    suspend fun login(@Body request: LoginRequest): ApiResponse<LoginResponse>

    @POST("api/v1/auth/signup")
    suspend fun signup(@Body request: SignupRequest): ApiResponse<LoginResponse>

    @GET("api/v1/auth/me")
    suspend fun getMe(): ApiResponse<User>

    // 修改昵称与头像
    @POST("api/v1/auth/profile")
    suspend fun updateProfile(@Body request: UpdateProfileRequest): ApiResponse<User>

    @GET("api/v1/mobile/dashboard")
    suspend fun getDashboard(): ApiResponse<MobileDashboard>

    @GET("api/v1/live/snapshot")
    suspend fun getLiveSnapshot(): ApiResponse<LiveSnapshot>

    @POST("api/v1/mobile/chat")
    suspend fun chat(@Body request: ChatRequest): ApiResponse<ChatResponse>

    @GET("api/v1/devices")
    suspend fun getDevices(): ApiResponse<List<Device>>

    // 添加雷达设备
    @POST("api/v1/devices")
    suspend fun createDevice(@Body request: CreateDeviceRequest): ApiResponse<Device>

    @DELETE("api/v1/devices/{id}")
    suspend fun deleteDevice(@Path("id") id: String): ApiResponse<DeleteResult>

    @GET("api/v1/alerts")
    suspend fun getAlerts(): ApiResponse<List<Alert>>
}
