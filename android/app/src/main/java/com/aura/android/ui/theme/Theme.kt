package com.aura.android.ui.theme

import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color

// 浅色主题配置，不跟随深色模式
private val AuraColorScheme = lightColorScheme(
    primary = PrimaryBlue,
    onPrimary = Color(0xFFF8F6F1),
    primaryContainer = PrimaryContainer,
    onPrimaryContainer = Color(0xFF2F4029),
    secondary = IOSGreen,
    onSecondary = Color(0xFFF8F6F1),
    secondaryContainer = SecondaryContainer,
    onSecondaryContainer = Color(0xFF43593B),
    tertiary = ChartHero,
    onTertiary = Color(0xFF3A2C08),
    background = IOSGray6,
    onBackground = IOSLabel,
    surface = Paper,
    onSurface = IOSLabel,
    surfaceVariant = SurfaceLow,
    onSurfaceVariant = IOSSecondaryLabel,
    outline = IOSSeparator,
    outlineVariant = IOSGray4,
    error = ErrorRed,
    onError = Color.White
)

@Composable
fun AuraTheme(
    content: @Composable () -> Unit
) {
    MaterialTheme(
        colorScheme = AuraColorScheme,
        typography = AuraTypography,
        shapes = AuraShapes,
        content = content
    )
}
