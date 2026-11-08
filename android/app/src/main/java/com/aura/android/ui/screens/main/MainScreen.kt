package com.aura.android.ui.screens.main

import androidx.compose.animation.AnimatedContentTransitionScope
import androidx.compose.animation.core.FastOutSlowInEasing
import androidx.compose.animation.core.tween
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.slideInVertically
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Devices
import androidx.compose.material.icons.filled.Home
import androidx.compose.material.icons.filled.Notifications
import androidx.compose.material.icons.filled.Person
import androidx.compose.material.icons.filled.Sensors
import androidx.compose.material.icons.filled.SmartToy
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.NavigationBar
import androidx.compose.material3.NavigationBarItem
import androidx.compose.material3.NavigationBarItemDefaults
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.navigation.NavDestination.Companion.hierarchy
import androidx.navigation.NavGraph.Companion.findStartDestination
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.currentBackStackEntryAsState
import androidx.navigation.compose.rememberNavController
import com.aura.android.data.datastore.TokenDataStore
import com.aura.android.ui.navigation.Screen
import com.aura.android.ui.theme.Hairline
import com.aura.android.ui.theme.Paper
import com.aura.android.ui.screens.alerts.AlertsScreen
import com.aura.android.ui.screens.alerts.AlertsViewModel
import com.aura.android.ui.screens.chat.ChatScreen
import com.aura.android.ui.screens.chat.ChatViewModel
import com.aura.android.ui.screens.dashboard.DashboardScreen
import com.aura.android.ui.screens.dashboard.DashboardViewModel
import com.aura.android.ui.screens.devices.DevicesScreen
import com.aura.android.ui.screens.devices.DevicesViewModel
import com.aura.android.ui.screens.live.LiveScreen
import com.aura.android.ui.screens.live.LiveViewModel
import com.aura.android.ui.screens.profile.ProfileScreen
import com.aura.android.ui.screens.profile.ProfileViewModel
import androidx.compose.ui.graphics.Color
import com.aura.android.ui.theme.SurfaceBright
import com.aura.android.ui.theme.Vermilion

// 主导航框架
@Composable
fun MainScreen(onLogout: () -> Unit) {
    val navController = rememberNavController()
    val context = LocalContext.current
    val tokenDataStore = remember { TokenDataStore(context) }
    val s = com.aura.android.ui.i18n.LocalStrings.current

    val items = listOf(
        BottomNavItem(s.nav.home, Icons.Default.Home, Screen.Dashboard.route),
        BottomNavItem(s.nav.live, Icons.Default.Sensors, Screen.Live.route),
        BottomNavItem(s.nav.devices, Icons.Default.Devices, Screen.Devices.route),
        BottomNavItem(s.nav.alerts, Icons.Default.Notifications, Screen.Alerts.route),
        BottomNavItem(s.nav.profile, Icons.Default.Person, Screen.Profile.route)
    )

    Scaffold(
        containerColor = Color.Transparent,
        contentWindowInsets = WindowInsets(0, 0, 0, 0),
        bottomBar = {
            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(start = 26.dp, end = 26.dp, top = 12.dp, bottom = 26.dp)
            ) {
                val navBackStackEntry by navController.currentBackStackEntryAsState()
                val currentDestination = navBackStackEntry?.destination
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .shadow(10.dp, RoundedCornerShape(28.dp), clip = false)
                        .clip(RoundedCornerShape(28.dp))
                        .background(Paper.copy(alpha = 0.90f))
                        .border(
                            width = 0.8.dp,
                            color = Hairline.copy(alpha = 0.55f),
                            shape = RoundedCornerShape(28.dp)
                        )
                        .padding(vertical = 7.dp),
                    horizontalArrangement = Arrangement.SpaceEvenly,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    items.forEach { item ->
                        val selected = currentDestination?.hierarchy
                            ?.any { it.route == item.route } == true
                        Column(
                            modifier = Modifier
                                .weight(1f)
                                .clip(RoundedCornerShape(20.dp))
                                .clickable {
                                    if (item.route == Screen.Dashboard.route) {
                                        // 返回起始页用 popBackStack
                                        navController.popBackStack(
                                            route = Screen.Dashboard.route,
                                            inclusive = false
                                        )
                                    } else {
                                        navController.navigate(item.route) {
                                            popUpTo(navController.graph.findStartDestination().id) {
                                                saveState = true
                                            }
                                            launchSingleTop = true
                                            restoreState = true
                                        }
                                    }
                                }
                                .padding(vertical = 5.dp),
                            horizontalAlignment = Alignment.CenterHorizontally
                        ) {
                            Icon(
                                item.icon,
                                contentDescription = item.label,
                                tint = if (selected) Vermilion else Color(0xFFB9A99A),
                                modifier = Modifier.size(21.dp)
                            )
                            Spacer(modifier = Modifier.height(3.dp))
                            Text(
                                item.label,
                                style = MaterialTheme.typography.labelSmall,
                                color = if (selected) Vermilion else Color(0xFFB9A99A)
                            )
                        }
                    }
                }
            }
        }
    ) { innerPadding ->
        NavHost(
            navController = navController,
            startDestination = Screen.Dashboard.route,
            // 只避让顶部
            modifier = Modifier.padding(top = innerPadding.calculateTopPadding()),
            // Tab 切换使用淡入上浮
            enterTransition = {
                fadeIn(tween(180)) + slideInVertically(
                    initialOffsetY = { it / 60 },
                    animationSpec = tween(180, easing = FastOutSlowInEasing)
                )
            },
            exitTransition = {
                fadeOut(tween(110))
            },
            popEnterTransition = {
                fadeIn(tween(180)) + slideInVertically(
                    initialOffsetY = { it / 60 },
                    animationSpec = tween(180, easing = FastOutSlowInEasing)
                )
            },
            popExitTransition = {
                fadeOut(tween(110))
            }
        ) {
            composable(Screen.Dashboard.route) {
                val viewModel: DashboardViewModel = viewModel()
                DashboardScreen(
                    viewModel = viewModel,
                    onOpenLive = { navController.navigate(Screen.Live.route) },
                    onOpenDevices = { navController.navigate(Screen.Devices.route) },
                    onOpenAlerts = { navController.navigate(Screen.Alerts.route) },
                    onOpenChat = { navController.navigate(Screen.Chat.route) },
                    onOpenTrend = { navController.navigate(Screen.Trend.route) }
                )
            }
            composable(Screen.Live.route) {
                val viewModel: LiveViewModel = viewModel()
                LiveScreen(viewModel = viewModel)
            }
            composable(Screen.Trend.route) {
                com.aura.android.ui.screens.trend.TrendScreen(
                    onBack = { navController.popBackStack() }
                )
            }
            composable(Screen.Chat.route) {
                val viewModel: ChatViewModel = viewModel(
                    factory = ChatViewModel.Factory(tokenDataStore)
                )
                ChatScreen(
                    viewModel = viewModel,
                    onBack = { navController.popBackStack() },
                    onOpenSettings = { navController.navigate(Screen.Profile.route) }
                )
            }
            composable(Screen.Devices.route) {
                val viewModel: DevicesViewModel = viewModel()
                DevicesScreen(viewModel = viewModel)
            }
            composable(Screen.Alerts.route) {
                val viewModel: AlertsViewModel = viewModel()
                AlertsScreen(viewModel = viewModel)
            }
            composable(Screen.Profile.route) {
                val viewModel: ProfileViewModel = viewModel(
                    factory = ProfileViewModel.Factory(tokenDataStore)
                )
                ProfileScreen(
                    viewModel = viewModel,
                    onLogout = onLogout
                )
            }
        }
    }
}

private data class BottomNavItem(
    val label: String,
    val icon: ImageVector,
    val route: String
)
