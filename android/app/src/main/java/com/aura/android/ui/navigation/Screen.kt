package com.aura.android.ui.navigation

// 应用路由定义
sealed class Screen(val route: String) {
    data object Login : Screen("login")
    data object Main : Screen("main")

    // 底部导航子页面
    data object Dashboard : Screen("dashboard")
    data object Live : Screen("live")
    data object Chat : Screen("chat")
    data object Trend : Screen("trend")
    data object Devices : Screen("devices")
    data object Alerts : Screen("alerts")
    data object Profile : Screen("profile")
}
