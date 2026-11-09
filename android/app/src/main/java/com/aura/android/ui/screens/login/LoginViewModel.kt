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
import com.aura.android.data.model.ApiResponse
import com.aura.android.data.model.LoginRequest
import com.aura.android.data.model.LoginResponse
import com.aura.android.data.model.PairLoginRequest
import com.aura.android.data.model.SignupRequest
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

    var name by mutableStateOf("")
        private set

    var pairCode by mutableStateOf("")
        private set

    var pairDialogVisible by mutableStateOf(false)
        private set

    var registerMode by mutableStateOf(false)
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

    fun onNameChange(value: String) {
        name = value
    }

    fun onPairCodeChange(value: String) {
        pairCode = value
    }

    fun onServerUrlChange(value: String) {
        serverUrl = value
    }

    fun toggleMode() {
        registerMode = !registerMode
        uiState = null
    }

    fun openPairDialog() {
        pairDialogVisible = true
        uiState = null
    }

    fun dismissPairDialog() {
        pairDialogVisible = false
    }

    fun login() {
        if (email.isBlank() || password.isBlank()) {
            uiState = ApiResult.Error("请输入邮箱和密码")
            return
        }
        submit { ApiClient.auraApi.login(LoginRequest(email.trim(), password)) }
    }

    fun register() {
        if (name.isBlank() || email.isBlank() || password.length < 8) {
            uiState = ApiResult.Error("昵称和邮箱必填，密码至少 8 位")
            return
        }
        submit {
            ApiClient.auraApi.signup(SignupRequest(name.trim(), email.trim(), password))
        }
    }

    // 仅用配对码登录，成功后自动进入本机归属账号
    fun loginWithPairCode() {
        if (pairCode.trim().length != 6) {
            uiState = ApiResult.Error("请输入 6 位配对码")
            return
        }
        submit { ApiClient.auraApi.pairLogin(PairLoginRequest(pairCode.trim())) }
    }

    private fun submit(call: suspend () -> ApiResponse<LoginResponse>) {
        viewModelScope.launch {
            uiState = ApiResult.Loading
            try {
                ApiClient.configureBaseUrl(serverUrl)
                tokenDataStore.saveServerUrl(serverUrl)
                when (val result = call().toApiResult()) {
                    is ApiResult.Success -> {
                        tokenDataStore.saveToken(result.data.token)
                        tokenDataStore.saveUserId(result.data.user.id)
                        pairDialogVisible = false
                        uiState = ApiResult.Success(Unit)
                        _loginSuccess.emit(Unit)
                    }
                    is ApiResult.Error -> uiState = result
                    is ApiResult.Loading -> Unit
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
