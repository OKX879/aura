package com.aura.android.ui.screens.profile

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.lifecycle.ViewModel
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewModelScope
import com.aura.android.data.DemoData
import com.aura.android.data.LocalMode
import com.aura.android.data.ai.AiConfig
import com.aura.android.data.api.ApiClient
import com.aura.android.data.api.ApiResult
import com.aura.android.data.api.toApiResult
import com.aura.android.data.datastore.TokenDataStore
import com.aura.android.data.model.UpdateProfileRequest
import com.aura.android.data.model.User
import com.aura.android.ui.i18n.AppLanguage
import com.aura.android.ui.i18n.I18n
import com.aura.android.ui.theme.Wallpaper
import com.aura.android.ui.theme.WallpaperPreset
import com.aura.android.ui.theme.saveWallpaperImage
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.launch

class ProfileViewModel(
    private val tokenDataStore: TokenDataStore
) : ViewModel() {

    var user by mutableStateOf<User?>(null)
        private set

    var uiState by mutableStateOf<ApiResult<User>?>(null)
        private set

    var serverUrl by mutableStateOf<String?>(null)
        private set

    var aiConfig by mutableStateOf(AiConfig())
        private set

    // 资料保存状态
    var saving by mutableStateOf(false)
        private set

    // 一次性提示文案
    var notice by mutableStateOf<String?>(null)
        private set

    init {
        viewModelScope.launch {
            serverUrl = tokenDataStore.serverUrl.first()
            aiConfig = AiConfig(
                baseUrl = tokenDataStore.aiBaseUrl.first() ?: AiConfig.DEFAULT_BASE_URL,
                apiKey = tokenDataStore.aiApiKey.first().orEmpty(),
                model = tokenDataStore.aiModel.first() ?: AiConfig.DEFAULT_MODEL
            ).normalized()
        }
        if (LocalMode.ENABLED) {
            user = DemoData.user
            uiState = ApiResult.Success(DemoData.user)
        } else {
            loadProfile()
        }
    }

    fun saveAiConfig(baseUrl: String, apiKey: String, model: String) {
        val config = AiConfig(baseUrl = baseUrl, apiKey = apiKey, model = model).normalized()
        aiConfig = config
        viewModelScope.launch {
            tokenDataStore.saveAiConfig(config.baseUrl, config.apiKey, config.model)
        }
    }

    fun updateServer(value: String) {
        val normalized = value.trim().trimEnd('/')
        if (!normalized.startsWith("http://") && !normalized.startsWith("https://")) return
        ApiClient.configureBaseUrl(normalized)
        viewModelScope.launch {
            tokenDataStore.saveServerUrl(normalized)
            serverUrl = normalized
            loadProfile()
        }
    }

    // 修改昵称
    fun updateName(value: String) {
        val name = value.trim()
        if (name.isEmpty()) {
            notice = "昵称不能为空"
            return
        }
        if (name == user?.name) return
        submitProfile(UpdateProfileRequest(name = name))
    }

    // 修改头像
    fun updateAvatar(base64: String?) {
        submitProfile(UpdateProfileRequest(avatar = base64))
    }

    fun clearNotice() {
        notice = null
    }

    // 切换语言
    fun setLanguage(value: AppLanguage) {
        I18n.set(value)
        viewModelScope.launch { tokenDataStore.saveLanguage(value.tag) }
    }

    // 切换内置壁纸
    fun setWallpaperPreset(preset: WallpaperPreset) {
        Wallpaper.applyPreset(preset)
        viewModelScope.launch { tokenDataStore.saveWallpaper(preset.key, null) }
    }

    // 从相册选择壁纸
    fun setWallpaperImage(context: android.content.Context, uri: android.net.Uri) {
        viewModelScope.launch {
            val path = saveWallpaperImage(context, uri)
            if (path == null) {
                notice = "图片读取失败"
                return@launch
            }
            Wallpaper.applyCustom(path)
            tokenDataStore.saveWallpaper(WallpaperPreset.CUSTOM.key, path)
            notice = "壁纸已更新"
        }
    }

    private fun submitProfile(request: UpdateProfileRequest) {
        if (LocalMode.ENABLED) {
            notice = "本地演示模式不支持修改资料"
            return
        }
        viewModelScope.launch {
            saving = true
            try {
                when (val result = ApiClient.auraApi.updateProfile(request).toApiResult()) {
                    is ApiResult.Success -> {
                        user = result.data
                        uiState = ApiResult.Success(result.data)
                        notice = "已保存"
                    }
                    is ApiResult.Error -> notice = result.message
                    else -> Unit
                }
            } catch (e: Exception) {
                notice = e.message ?: "保存失败"
            } finally {
                saving = false
            }
        }
    }

    fun loadProfile() {
        if (LocalMode.ENABLED) {
            user = DemoData.user
            uiState = ApiResult.Success(DemoData.user)
            return
        }
        viewModelScope.launch {
            uiState = ApiResult.Loading
            try {
                val response = ApiClient.auraApi.getMe()
                val result = response.toApiResult()
                if (result is ApiResult.Success) {
                    user = result.data
                }
                uiState = result
            } catch (e: Exception) {
                uiState = ApiResult.Error(e.message ?: "加载失败")
            }
        }
    }

    fun logout(onComplete: () -> Unit) {
        viewModelScope.launch {
            tokenDataStore.clear()
            onComplete()
        }
    }

    class Factory(
        private val tokenDataStore: TokenDataStore
    ) : ViewModelProvider.Factory {
        @Suppress("UNCHECKED_CAST")
        override fun <T : ViewModel> create(modelClass: Class<T>): T {
            return ProfileViewModel(tokenDataStore) as T
        }
    }
}
