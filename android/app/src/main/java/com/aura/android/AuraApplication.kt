package com.aura.android

import android.app.Application
import com.aura.android.data.AlertHistory
import com.aura.android.data.api.ApiClient
import com.aura.android.data.datastore.TokenDataStore
import com.aura.android.ui.i18n.AppLanguage
import com.aura.android.ui.i18n.I18n
import com.aura.android.ui.theme.Wallpaper
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.runBlocking

// 应用入口
class AuraApplication : Application() {

    lateinit var tokenDataStore: TokenDataStore
        private set

    override fun onCreate() {
        super.onCreate()
        tokenDataStore = TokenDataStore(this)
        AlertHistory.init(this)

        runBlocking {
            tokenDataStore.serverUrl.first()?.let(ApiClient::configureBaseUrl)
            // 提前恢复语言与壁纸
            I18n.set(AppLanguage.fromTag(tokenDataStore.languageTag.first()))
            Wallpaper.restore(
                tokenDataStore.wallpaperPreset.first(),
                tokenDataStore.wallpaperPath.first()
            )
        }

        // 注入 Token 提供器
        ApiClient.setTokenProvider {
            runBlocking { tokenDataStore.token.first() }
        }
    }
}
