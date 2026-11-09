package com.aura.android.data.api

import com.aura.android.BuildConfig
import com.jakewharton.retrofit2.converter.kotlinx.serialization.asConverterFactory
import kotlinx.serialization.json.Json
import okhttp3.MediaType.Companion.toMediaType
import okhttp3.OkHttpClient
import okhttp3.logging.HttpLoggingInterceptor
import retrofit2.Retrofit
import java.util.concurrent.TimeUnit

// Retrofit 客户端工厂
object ApiClient {

    // 默认服务地址
    const val DEFAULT_BASE_URL = "https://aura-x5.tailde76b2.ts.net/"

    private val json = Json {
        ignoreUnknownKeys = true
        coerceInputValues = true
        isLenient = true
    }

    private var authInterceptor: AuthInterceptor? = null
    @Volatile private var baseUrl = DEFAULT_BASE_URL
    @Volatile private var apiInstance: AuraApi? = null

    // 注入 Token 提供器
    fun setTokenProvider(provider: () -> String?) {
        authInterceptor = AuthInterceptor(provider)
        apiInstance = null
    }

    fun configureBaseUrl(value: String) {
        val normalized = value.trim().let { if (it.endsWith('/')) it else "$it/" }
        require(normalized.startsWith("http://") || normalized.startsWith("https://")) {
            "服务器地址必须以 http:// 或 https:// 开头"
        }
        if (normalized != baseUrl) {
            baseUrl = normalized
            apiInstance = null
        }
    }

    // 当前服务地址
    val currentBaseUrl: String
        get() = baseUrl

    private fun createClient(): OkHttpClient {
        val builder = OkHttpClient.Builder()
            .connectTimeout(10, TimeUnit.SECONDS)
            .readTimeout(120, TimeUnit.SECONDS)
            .writeTimeout(30, TimeUnit.SECONDS)
            .apply {
                authInterceptor?.let { addInterceptor(it) }
            }
        if (BuildConfig.DEBUG) {
            builder.addInterceptor(HttpLoggingInterceptor().apply {
                level = HttpLoggingInterceptor.Level.BASIC
            })
        }
        return builder.build()
    }

    private fun createApi(): AuraApi {
        return Retrofit.Builder()
            .baseUrl(baseUrl)
            .client(createClient())
            .addConverterFactory(json.asConverterFactory("application/json".toMediaType()))
            .build()
            .create(AuraApi::class.java)
    }

    val auraApi: AuraApi
        get() = apiInstance ?: synchronized(this) {
            apiInstance ?: createApi().also { apiInstance = it }
        }
}
