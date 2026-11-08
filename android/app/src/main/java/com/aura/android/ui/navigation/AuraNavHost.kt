package com.aura.android.ui.navigation

import androidx.compose.animation.AnimatedContentTransitionScope
import androidx.compose.animation.core.tween
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.navigation.NavHostController
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.rememberNavController
import com.aura.android.data.LocalMode
import com.aura.android.data.datastore.TokenDataStore
import com.aura.android.data.session.SessionEvents
import com.aura.android.ui.screens.login.LoginScreen
import com.aura.android.ui.screens.login.LoginViewModel
import com.aura.android.ui.screens.main.MainScreen
import com.aura.android.ui.theme.Hint
import com.aura.android.ui.theme.PrimaryBlue
import com.aura.android.ui.theme.Surface
import kotlinx.coroutines.flow.first

// 应用根导航
@Composable
fun AuraNavHost(navController: NavHostController = rememberNavController()) {
    val context = LocalContext.current
    val tokenDataStore = TokenDataStore(context)
    var startDestination by remember { mutableStateOf<String?>(null) }

    LaunchedEffect(Unit) {
        if (LocalMode.ENABLED) {
            // 本地模式跳过登录
            startDestination = Screen.Main.route
            return@LaunchedEffect
        }
        val token = tokenDataStore.token.first()
        startDestination = if (token.isNullOrBlank()) {
            Screen.Login.route
        } else {
            Screen.Main.route
        }
    }

    // 登录失效时回到登录页
    LaunchedEffect(Unit) {
        SessionEvents.expired.collect {
            tokenDataStore.clear()
            SessionEvents.reset()
            navController.navigate(Screen.Login.route) {
                popUpTo(Screen.Main.route) { inclusive = true }
                launchSingleTop = true
            }
        }
    }

    val route = startDestination
    if (route == null) {
        Column(
            modifier = Modifier.fillMaxSize().background(Surface),
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.Center
        ) {
            CircularProgressIndicator(color = PrimaryBlue)
            Text(
                text = "AURA",
                style = MaterialTheme.typography.labelSmall.copy(color = Hint),
                modifier = Modifier.padding(top = 12.dp)
            )
        }
        return
    }

    NavHost(
        navController = navController,
        startDestination = route,
        enterTransition = {
            slideIntoContainer(
                AnimatedContentTransitionScope.SlideDirection.Start,
                animationSpec = tween(300)
            )
        },
        exitTransition = {
            slideOutOfContainer(
                AnimatedContentTransitionScope.SlideDirection.Start,
                animationSpec = tween(300)
            )
        },
        popEnterTransition = {
            slideIntoContainer(
                AnimatedContentTransitionScope.SlideDirection.End,
                animationSpec = tween(300)
            )
        },
        popExitTransition = {
            slideOutOfContainer(
                AnimatedContentTransitionScope.SlideDirection.End,
                animationSpec = tween(300)
            )
        }
    ) {
        composable(Screen.Login.route) {
            val loginViewModel: LoginViewModel = viewModel(
                factory = LoginViewModel.Factory(tokenDataStore)
            )
            LoginScreen(
                viewModel = loginViewModel,
                onLoginSuccess = {
                    SessionEvents.reset()
                    navController.navigate(Screen.Main.route) {
                        popUpTo(Screen.Login.route) { inclusive = true }
                    }
                }
            )
        }

        composable(Screen.Main.route) {
            MainScreen(
                onLogout = {
                    navController.navigate(Screen.Login.route) {
                        popUpTo(Screen.Main.route) { inclusive = true }
                    }
                }
            )
        }
    }
}
