package com.aura.android.data.api

import com.aura.android.data.session.SessionEvents
import kotlinx.coroutines.runBlocking
import okhttp3.Interceptor
import okhttp3.Response

// 附加 Token 的拦截器
class AuthInterceptor(
    private val tokenProvider: () -> String?
) : Interceptor {

    override fun intercept(chain: Interceptor.Chain): Response {
        val request = chain.request()
        val token = runBlocking { tokenProvider() }

        val response = if (!token.isNullOrBlank()) {
            chain.proceed(
                request.newBuilder()
                    .header("Authorization", "Bearer $token")
                    .header("Accept", "application/json")
                    .header("Content-Type", "application/json")
                    .build()
            )
        } else {
            chain.proceed(request)
        }

        if (response.code == 401 && !token.isNullOrBlank()) {
            SessionEvents.notifyExpired()
        }
        return response
    }
}
