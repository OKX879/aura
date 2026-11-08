package com.aura.android

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.toArgb
import androidx.core.view.WindowCompat
import com.aura.android.ui.i18n.I18n
import com.aura.android.ui.i18n.LocalStrings
import com.aura.android.ui.navigation.AuraNavHost
import com.aura.android.ui.theme.AuraTheme
import com.aura.android.ui.theme.Surface
import com.aura.android.ui.theme.WallpaperBackground

class MainActivity : ComponentActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        window.statusBarColor = Surface.toArgb()
        // 导航条与背景同色
        window.navigationBarColor = Surface.toArgb()
        WindowCompat.getInsetsController(window, window.decorView)
            .isAppearanceLightStatusBars = true
        WindowCompat.getInsetsController(window, window.decorView)
            .isAppearanceLightNavigationBars = true
        setContent {
            AuraTheme {
                AuraApp()
            }
        }
    }
}

@Composable
fun AuraApp() {
    // 读取语言状态以触发重组
    val language = I18n.language
    val strings = remember(language) { I18n.strings }
    Box(modifier = Modifier.fillMaxSize()) {
        WallpaperBackground()
        CompositionLocalProvider(LocalStrings provides strings) {
            AuraNavHost()
        }
    }
}
