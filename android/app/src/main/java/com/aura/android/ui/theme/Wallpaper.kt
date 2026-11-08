package com.aura.android.ui.theme

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.net.Uri
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.ContentScale
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.File
import java.io.FileOutputStream

// 内置渐变壁纸，支持相册自选
enum class WallpaperPreset(val key: String) {
    DEFAULT("default"),
    WARM("warm"),
    MIST("mist"),
    DEEP("deep"),
    MORNING("morning"),
    CUSTOM("custom");

    companion object {
        fun fromKey(key: String?): WallpaperPreset =
            entries.firstOrNull { it.key == key } ?: DEFAULT
    }
}

object Wallpaper {
    var preset by mutableStateOf(WallpaperPreset.DEFAULT)
        private set

    var customPath by mutableStateOf<String?>(null)
        private set

    fun restore(presetKey: String?, path: String?) {
        preset = WallpaperPreset.fromKey(presetKey)
        customPath = path
    }

    fun applyPreset(value: WallpaperPreset) {
        preset = value
        if (value != WallpaperPreset.CUSTOM) customPath = null
    }

    fun applyCustom(path: String) {
        preset = WallpaperPreset.CUSTOM
        customPath = path
    }
}

// 铺满整屏的背景
@androidx.compose.runtime.Composable
fun WallpaperBackground(modifier: Modifier = Modifier) {
    when (Wallpaper.preset) {
        WallpaperPreset.DEFAULT -> Box(
            modifier.fillMaxSize().background(Surface)
        )
        WallpaperPreset.WARM -> Box(
            modifier.fillMaxSize().background(
                Brush.verticalGradient(listOf(Color(0xFFF8F1E8), Color(0xFFE7D9C7)))
            )
        )
        WallpaperPreset.MIST -> Box(
            modifier.fillMaxSize().background(
                Brush.verticalGradient(listOf(Color(0xFFEEF2F0), Color(0xFFD5E0DA)))
            )
        )
        WallpaperPreset.DEEP -> Box(
            modifier.fillMaxSize().background(
                Brush.verticalGradient(listOf(Color(0xFFE9EDF3), Color(0xFFC9D4E2)))
            )
        )
        WallpaperPreset.MORNING -> Box(
            modifier.fillMaxSize().background(
                Brush.verticalGradient(listOf(Color(0xFFFFF4E9), Color(0xFFF2D6C2)))
            )
        )
        WallpaperPreset.CUSTOM -> {
            val path = Wallpaper.customPath
            val bitmap = remember(path) {
                path?.let {
                    runCatching { BitmapFactory.decodeFile(it)?.asImageBitmap() }.getOrNull()
                }
            }
            if (bitmap != null) {
                Image(
                    bitmap = bitmap,
                    contentDescription = null,
                    contentScale = ContentScale.Crop,
                    modifier = modifier.fillMaxSize()
                )
            } else {
                Box(modifier.fillMaxSize().background(Surface))
            }
        }
    }
}

// 压缩并保存壁纸图片，返回路径
suspend fun saveWallpaperImage(context: Context, uri: Uri): String? =
    withContext(Dispatchers.IO) {
        try {
            val target = File(context.filesDir, "wallpaper.jpg")
            val raw = context.contentResolver.openInputStream(uri)?.use {
                BitmapFactory.decodeStream(it)
            } ?: return@withContext null
            val maxSide = 1600f
            val scale = minOf(1f, maxSide / maxOf(raw.width, raw.height))
            val scaled: Bitmap = if (scale >= 1f) raw else Bitmap.createScaledBitmap(
                raw,
                (raw.width * scale).toInt().coerceAtLeast(1),
                (raw.height * scale).toInt().coerceAtLeast(1),
                true
            )
            FileOutputStream(target).use { output ->
                scaled.compress(Bitmap.CompressFormat.JPEG, 88, output)
            }
            target.absolutePath
        } catch (e: Exception) {
            null
        }
    }
