package com.aura.android.data.datastore

import android.content.Context
import androidx.datastore.core.DataStore
import androidx.datastore.preferences.core.Preferences
import androidx.datastore.preferences.core.edit
import androidx.datastore.preferences.core.stringPreferencesKey
import androidx.datastore.preferences.preferencesDataStore
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.map

private val Context.dataStore: DataStore<Preferences> by preferencesDataStore(name = "aura_prefs")

// Token 本地持久化
class TokenDataStore(private val context: Context) {

    companion object {
        private val TOKEN_KEY = stringPreferencesKey("auth_token")
        private val USER_ID_KEY = stringPreferencesKey("user_id")
        private val SERVER_URL_KEY = stringPreferencesKey("server_url")
        private val AI_BASE_URL_KEY = stringPreferencesKey("ai_base_url")
        private val AI_API_KEY_KEY = stringPreferencesKey("ai_api_key")
        private val AI_MODEL_KEY = stringPreferencesKey("ai_model")
        private val LANGUAGE_KEY = stringPreferencesKey("language_tag")
        private val WALLPAPER_PRESET_KEY = stringPreferencesKey("wallpaper_preset")
        private val WALLPAPER_PATH_KEY = stringPreferencesKey("wallpaper_path")
    }

    val token: Flow<String?> = context.dataStore.data.map { prefs ->
        prefs[TOKEN_KEY]
    }

    val userId: Flow<String?> = context.dataStore.data.map { prefs ->
        prefs[USER_ID_KEY]
    }

    val serverUrl: Flow<String?> = context.dataStore.data.map { prefs ->
        prefs[SERVER_URL_KEY]
    }

    val aiBaseUrl: Flow<String?> = context.dataStore.data.map { prefs ->
        prefs[AI_BASE_URL_KEY]
    }

    val aiApiKey: Flow<String?> = context.dataStore.data.map { prefs ->
        prefs[AI_API_KEY_KEY]
    }

    val aiModel: Flow<String?> = context.dataStore.data.map { prefs ->
        prefs[AI_MODEL_KEY]
    }

    val languageTag: Flow<String?> = context.dataStore.data.map { prefs ->
        prefs[LANGUAGE_KEY]
    }

    val wallpaperPreset: Flow<String?> = context.dataStore.data.map { prefs ->
        prefs[WALLPAPER_PRESET_KEY]
    }

    val wallpaperPath: Flow<String?> = context.dataStore.data.map { prefs ->
        prefs[WALLPAPER_PATH_KEY]
    }

    suspend fun saveToken(token: String) {
        context.dataStore.edit { prefs ->
            prefs[TOKEN_KEY] = token
        }
    }

    suspend fun saveUserId(userId: String) {
        context.dataStore.edit { prefs ->
            prefs[USER_ID_KEY] = userId
        }
    }

    suspend fun saveServerUrl(serverUrl: String) {
        context.dataStore.edit { prefs ->
            prefs[SERVER_URL_KEY] = serverUrl
        }
    }

    // 保存 AI 接口配置
    suspend fun saveAiConfig(baseUrl: String, apiKey: String, model: String) {
        context.dataStore.edit { prefs ->
            prefs[AI_BASE_URL_KEY] = baseUrl
            prefs[AI_API_KEY_KEY] = apiKey
            prefs[AI_MODEL_KEY] = model
        }
    }

    suspend fun saveLanguage(tag: String) {
        context.dataStore.edit { prefs ->
            prefs[LANGUAGE_KEY] = tag
        }
    }

    // 保存壁纸设置
    suspend fun saveWallpaper(preset: String, path: String?) {
        context.dataStore.edit { prefs ->
            prefs[WALLPAPER_PRESET_KEY] = preset
            if (path == null) prefs.remove(WALLPAPER_PATH_KEY) else prefs[WALLPAPER_PATH_KEY] = path
        }
    }

    suspend fun clear() {
        context.dataStore.edit { prefs ->
            prefs.remove(TOKEN_KEY)
            prefs.remove(USER_ID_KEY)
        }
    }
}
