package com.aura.android.ui.screens.login

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.lifecycle.ViewModel
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewModelScope
import com.aura.android.data.api.ApiClient
import com.aura.android.data.api.ApiResult
import com.aura.android.data.api.toApiResult
import com.aura.android.data.datastore.TokenDataStore
import com.aura.android.data.model.LoginRequest
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.launch

class LoginViewModel(
    private val tokenDataStore: TokenDataStore
) : ViewModel() {

    var email by mutableStateOf("")
        private set

    var password by mutableStateOf("")
        private set

    var serverUrl by mutableStateOf(ApiClient.DEFAULT_BASE_URL.removeSuffix("/"))
        private set

    var uiState by mutableStateOf<ApiResult<Unit>?>(null)
        private set

    private val _loginSuccess = MutableSharedFlow<Unit>()
    val loginSuccess: SharedFlow<Unit> = _loginSuccess

    init {
        viewModelScope.launch {
            tokenDataStore.serverUrl.first()?.let {
                serverUrl = it.removeSuffix("/")
            }
        }
    }

    fun onEmailChange(value: String) {
        email = value
    }

    fun onPasswordChange(value: String) {
        password = value
    }

    fun onServerUrlChange(value: String) {
        serverUrl = value
    }

    fun login() {
        if (email.isBlank() || password.isBlank()) {
            uiState = ApiResult.Error("请输入邮箱和密码")
            return
        }

        viewModelScope.launch {
            uiState = ApiResult.Loading
            try {
                ApiClient.configureBaseUrl(serverUrl)
                tokenDataStore.saveServerUrl(serverUrl)
                val response = ApiClient.auraApi.login(
                    LoginRequest(email = email.trim(), password = password)
                )
                val result = response.toApiResult()
                if (result is ApiResult.Success) {
                    tokenDataStore.saveToken(result.data.token)
                    tokenDataStore.saveUserId(result.data.user.id)
                    uiState = ApiResult.Success(Unit)
                    _loginSuccess.emit(Unit)
                } else if (result is ApiResult.Error) {
                    uiState = result
                }
            } catch (e: Exception) {
                uiState = ApiResult.Error(e.message ?: "网络异常，请检查服务器地址和 Wi-Fi")
            }
        }
    }

    class Factory(
        private val tokenDataStore: TokenDataStore
    ) : ViewModelProvider.Factory {
        @Suppress("UNCHECKED_CAST")
        override fun <T : ViewModel> create(modelClass: Class<T>): T {
            return LoginViewModel(tokenDataStore) as T
        }
    }
}
