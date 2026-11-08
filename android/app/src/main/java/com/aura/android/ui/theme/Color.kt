package com.aura.android.ui.theme

import androidx.compose.ui.graphics.Color

// 全局配色定义
val IOSBlue = Color(0xFF43593B)        // 主数据色
val IOSGreen = Color(0xFF77835A)       // 中绿
val IOSOrange = Color(0xFFD4A017)      // 强调色
val IOSRed = Color(0xFFB4472B)         // 告警色
val IOSPurple = Color(0xFF77835A)
val IOSTeal = Color(0xFFACAD79)        // 淡橄榄
val IOSYellow = Color(0xFFF2D17E)      // 麦黄

val IOSGray = Color(0xFF8A7768)        // 次要文字
val IOSGray2 = Color(0xFFB9A99A)       // 弱化文字
val IOSGray3 = Color(0xFFDCD3C8)       // 网格线
val IOSGray4 = Color(0xFFE2DBD2)       // 轨道色
val IOSGray5 = Color(0xFFE9E4DC)       // 静默色
val IOSGray6 = Color(0xFFF0EFEB)       // 背景色

val IOSLabel = Color(0xFF58402E)       // 主文字色
val IOSSecondaryLabel = Color(0xFF6E5A4A)  // 次要文字色
val IOSTertiaryLabel = Color(0xFFB9A99A)   // 弱化文字色
val IOSSeparator = Color(0xFFDCD3C8)

// 卡片背景色
val Paper = Color(0xFFF8F6F1)
val Surface = IOSGray6
val SurfaceBright = Paper
val SurfaceLow = Color(0xFFEDEBE4)
val SurfaceContainer = IOSGray5
val SurfaceHigh = IOSGray4
val SurfaceHighest = IOSGray3

val OnSurface = IOSLabel
val OnSurfaceVariant = IOSSecondaryLabel
val Muted = IOSSecondaryLabel
val Hint = IOSTertiaryLabel

val PrimaryBlue = IOSBlue
val PrimaryBlueSoft = Color(0xFFACAD79)
val PrimaryContainer = Color(0xFFDDE2CE)
val SecondaryContainer = Color(0xFFE6E8DC)

val Success = Color(0xFF43593B)
val SuccessSoft = Color(0xFFE4E8DA)
val Warning = Color(0xFFD4A017)
val WarningSoft = Color(0xFFF7EED6)
val ErrorRed = Color(0xFFB4472B)
val ErrorSoft = Color(0xFFF5E4DE)

val Hairline = IOSSeparator
val HairlineStrong = IOSGray2

// 发丝线颜色
val HairlineStroke = Color(0xFF8A7768).copy(alpha = 0.85f)

// 图表渐变色阶
val Ramp6 = listOf(
    Color(0xFF43593B), Color(0xFF5A7049), Color(0xFF77835A),
    Color(0xFF929960), Color(0xFFACAD79), Color(0xFFF2D17E)
)
val Cat4 = listOf(
    Color(0xFF43593B), Color(0xFF77835A),
    Color(0xFFACAD79), Color(0xFFF2D17E)
)
val ChartHero = Color(0xFFD4A017)
val ChartData = Color(0xFF43593B)
val ChartGrid = Color(0xFFDCD3C8)
val ChartTrack = Color(0xFFE2DBD2)

// 旧命名兼容
val Ink = IOSLabel
val Oat = IOSGray6
val Cream = Paper
val CreamSoft = Paper
val Forest = IOSBlue
val Forest2 = Color(0xFF5A7049)
val Forest3 = Color(0xFFACAD79)
val Vermilion = IOSBlue
val VermilionLight = Color(0xFFACAD79)
val Border = IOSSeparator
val BorderDark = IOSGray3
val MutedSoft = IOSTertiaryLabel

val AppleBlue = IOSBlue
val AppleBlueLight = Color(0xFFACAD79)
val BackgroundPrimary = IOSGray6
val BackgroundSecondary = IOSGray5
val BackgroundTertiary = IOSGray4
val LabelPrimary = IOSLabel
val LabelSecondary = IOSSecondaryLabel
val LabelTertiary = IOSTertiaryLabel
val Separator = IOSSeparator
val SuccessGreen = Success
val WarningOrange = Warning
val DangerRed = ErrorRed
