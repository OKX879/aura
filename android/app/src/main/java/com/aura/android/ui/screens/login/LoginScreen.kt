package com.aura.android.ui.screens.login

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Email
import androidx.compose.material.icons.filled.Link
import androidx.compose.material.icons.filled.Lock
import androidx.compose.material.icons.filled.Person
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TextField
import androidx.compose.material3.TextFieldDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.text.input.VisualTransformation
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aura.android.data.api.ApiResult
import com.aura.android.ui.components.AuraButton
import com.aura.android.ui.theme.ErrorRed
import com.aura.android.ui.theme.Hint
import com.aura.android.ui.theme.Muted
import com.aura.android.ui.theme.OnSurface
import com.aura.android.ui.theme.PrimaryBlue
import com.aura.android.ui.theme.Surface
import com.aura.android.ui.theme.SurfaceContainer

// 登录页
@Composable
fun LoginScreen(
    viewModel: LoginViewModel,
    onLoginSuccess: () -> Unit
) {
    LaunchedEffect(Unit) {
        viewModel.loginSuccess.collect { onLoginSuccess() }
    }

    val s = com.aura.android.ui.i18n.LocalStrings.current
    Column(
        modifier = Modifier
            .fillMaxSize()
            .background(Color.Transparent)
            .imePadding()
            .verticalScroll(rememberScrollState())
            .statusBarsPadding()
            .padding(horizontal = 24.dp, vertical = 32.dp)
    ) {
        Spacer(modifier = Modifier.height(28.dp))
        Box(
            modifier = Modifier
                .size(72.dp)
                .clip(CircleShape)
                .background(PrimaryBlue),
            contentAlignment = Alignment.Center
        ) {
            Text(
                text = "A",
                style = MaterialTheme.typography.displayMedium.copy(
                    color = Color.White,
                    fontSize = 36.sp
                )
            )
        }
        Spacer(modifier = Modifier.height(20.dp))
        Text(
            text = s.login.title,
            style = MaterialTheme.typography.displayLarge.copy(
                color = OnSurface,
                fontSize = 32.sp
            )
        )
        Spacer(modifier = Modifier.height(6.dp))
        Text(
            text = s.login.subtitle,
            style = MaterialTheme.typography.bodyMedium.copy(color = Muted)
        )
        Spacer(modifier = Modifier.height(28.dp))

        IosField(
            label = s.login.server,
            value = viewModel.serverUrl,
            onValueChange = viewModel::onServerUrlChange,
            icon = Icons.Default.Link,
            keyboardType = KeyboardType.Uri
        )
        Spacer(modifier = Modifier.height(14.dp))
        if (viewModel.registerMode) {
            IosField(
                label = s.login.name,
                value = viewModel.name,
                onValueChange = viewModel::onNameChange,
                icon = Icons.Default.Person,
                keyboardType = KeyboardType.Text
            )
            Spacer(modifier = Modifier.height(14.dp))
        }
        IosField(
            label = s.login.email,
            value = viewModel.email,
            onValueChange = viewModel::onEmailChange,
            icon = Icons.Default.Email,
            keyboardType = KeyboardType.Email
        )
        Spacer(modifier = Modifier.height(14.dp))
        IosField(
            label = s.login.password,
            value = viewModel.password,
            onValueChange = viewModel::onPasswordChange,
            icon = Icons.Default.Lock,
            keyboardType = KeyboardType.Password,
            isPassword = true,
            imeAction = ImeAction.Done
        )

        Spacer(modifier = Modifier.height(22.dp))

        when (val state = viewModel.uiState) {
            is ApiResult.Error -> {
                Text(
                    text = state.message,
                    color = ErrorRed,
                    style = MaterialTheme.typography.bodyMedium,
                    modifier = Modifier.padding(bottom = 12.dp)
                )
            }
            is ApiResult.Loading -> {
                CircularProgressIndicator(
                    color = PrimaryBlue,
                    modifier = Modifier
                        .padding(bottom = 12.dp)
                        .height(22.dp)
                )
            }
            else -> Unit
        }

        AuraButton(
            text = if (viewModel.registerMode) s.login.register else s.login.signIn,
            onClick = if (viewModel.registerMode) viewModel::register else viewModel::login,
            enabled = viewModel.uiState != ApiResult.Loading
        )

        TextButton(
            onClick = viewModel::toggleMode,
            modifier = Modifier.fillMaxWidth()
        ) {
            Text(
                text = if (viewModel.registerMode) s.login.switchToLogin else s.login.switchToRegister,
                color = PrimaryBlue,
                style = MaterialTheme.typography.bodyMedium
            )
        }

        Spacer(modifier = Modifier.height(8.dp))
        Text(
            text = s.login.hint,
            style = MaterialTheme.typography.labelSmall.copy(color = Hint),
            modifier = Modifier.fillMaxWidth()
        )
        Spacer(modifier = Modifier.height(32.dp))
    }
}

@Composable
private fun IosField(
    label: String,
    value: String,
    onValueChange: (String) -> Unit,
    icon: androidx.compose.ui.graphics.vector.ImageVector,
    keyboardType: KeyboardType,
    imeAction: ImeAction = ImeAction.Next,
    isPassword: Boolean = false
) {
    Column {
        Text(
            text = label,
            style = MaterialTheme.typography.labelSmall.copy(color = Hint),
            modifier = Modifier.padding(start = 4.dp, bottom = 6.dp)
        )
        TextField(
            value = value,
            onValueChange = onValueChange,
            singleLine = true,
            leadingIcon = {
                Icon(icon, contentDescription = null, tint = Hint)
            },
            visualTransformation = if (isPassword) {
                PasswordVisualTransformation()
            } else {
                VisualTransformation.None
            },
            keyboardOptions = KeyboardOptions(
                keyboardType = keyboardType,
                imeAction = imeAction
            ),
            shape = RoundedCornerShape(12.dp),
            colors = TextFieldDefaults.colors(
                focusedContainerColor = SurfaceContainer,
                unfocusedContainerColor = SurfaceContainer,
                focusedIndicatorColor = Color.Transparent,
                unfocusedIndicatorColor = Color.Transparent,
                focusedTextColor = OnSurface,
                unfocusedTextColor = OnSurface,
                cursorColor = PrimaryBlue
            ),
            modifier = Modifier.fillMaxWidth()
        )
    }
}
