package com.aura.android.ui.screens.profile

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.net.Uri
import android.util.Base64
import android.widget.Toast
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.PickVisualMediaRequest
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.Visibility
import androidx.compose.material.icons.filled.VisibilityOff
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.text.input.VisualTransformation
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aura.android.data.LocalMode
import com.aura.android.data.ai.AiConfig
import com.aura.android.data.api.ApiClient
import com.aura.android.data.api.ApiResult
import com.aura.android.data.model.User
import com.aura.android.ui.components.AuraCard
import com.aura.android.ui.components.LieCard
import com.aura.android.ui.components.LieEyebrow
import com.aura.android.ui.i18n.AppLanguage
import com.aura.android.ui.i18n.I18n
import com.aura.android.ui.i18n.Strings
import com.aura.android.ui.theme.ErrorRed
import com.aura.android.ui.theme.Hairline
import com.aura.android.ui.theme.HairlineStrong
import com.aura.android.ui.theme.Hint
import com.aura.android.ui.theme.Muted
import com.aura.android.ui.theme.OnSurface
import com.aura.android.ui.theme.Paper
import com.aura.android.ui.theme.PrimaryBlue
import com.aura.android.ui.theme.PrimaryContainer
import com.aura.android.ui.theme.SecondaryContainer
import com.aura.android.ui.theme.Surface
import com.aura.android.ui.theme.Wallpaper
import com.aura.android.ui.theme.WallpaperPreset
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.ByteArrayOutputStream

// 我的页
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ProfileScreen(
    viewModel: ProfileViewModel,
    onLogout: () -> Unit
) {
    val scrollState = rememberScrollState()
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    val s = com.aura.android.ui.i18n.LocalStrings.current
    var showServerDialog by remember { mutableStateOf(false) }
    var serverDraft by remember { mutableStateOf("") }
    var notificationsOn by remember { mutableStateOf(true) }
    var showAiDialog by remember { mutableStateOf(false) }
    var aiBaseDraft by remember { mutableStateOf("") }
    var aiKeyDraft by remember { mutableStateOf("") }
    var aiModelDraft by remember { mutableStateOf("") }
    var showNameDialog by remember { mutableStateOf(false) }
    var nameDraft by remember { mutableStateOf("") }
    var showLanguageDialog by remember { mutableStateOf(false) }
    var showWallpaperDialog by remember { mutableStateOf(false) }

    // 壁纸选择器
    val pickWallpaper = rememberLauncherForActivityResult(
        ActivityResultContracts.PickVisualMedia()
    ) { uri ->
        if (uri != null) {
            viewModel.setWallpaperImage(context, uri)
            showWallpaperDialog = false
        }
    }

    // 头像选择器
    val pickAvatar = rememberLauncherForActivityResult(
        ActivityResultContracts.PickVisualMedia()
    ) { uri ->
        if (uri != null) {
            scope.launch {
                val encoded = encodeAvatar(context, uri)
                if (encoded != null) {
                    viewModel.updateAvatar(encoded)
                } else {
                    Toast.makeText(context, "图片读取失败，请换一张", Toast.LENGTH_SHORT).show()
                }
            }
        }
    }

    LaunchedEffect(viewModel.notice) {
        viewModel.notice?.let { message ->
            Toast.makeText(context, message, Toast.LENGTH_SHORT).show()
            viewModel.clearNotice()
        }
    }

    Scaffold(
        containerColor = Color.Transparent,
        topBar = {
            ProfileNav(s = s, saving = viewModel.saving, onRefresh = viewModel::loadProfile)
        }
    ) { padding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .verticalScroll(scrollState)
                .padding(padding)
                .padding(horizontal = 24.dp)
        ) {
            when (val state = viewModel.uiState) {
                is ApiResult.Loading -> {
                    Box(
                        modifier = Modifier.fillMaxWidth().padding(vertical = 48.dp),
                        contentAlignment = Alignment.Center
                    ) {
                        CircularProgressIndicator(color = PrimaryBlue)
                    }
                }
                is ApiResult.Error -> {
                    Text(
                        text = state.message,
                        color = ErrorRed,
                        style = MaterialTheme.typography.bodyLarge,
                        modifier = Modifier.padding(vertical = 24.dp)
                    )
                }
                is ApiResult.Success -> {
                    val profile = state.data
                    ProfileCard(
                        s = s,
                        user = profile,
                        onPickAvatar = {
                            pickAvatar.launch(
                                PickVisualMediaRequest(
                                    ActivityResultContracts.PickVisualMedia.ImageOnly
                                )
                            )
                        },
                        onEditName = {
                            nameDraft = profile.name
                            showNameDialog = true
                        }
                    )

                    Spacer(modifier = Modifier.height(20.dp))
                    ServerCard(
                        s = s,
                        url = viewModel.serverUrl ?: ApiClient.DEFAULT_BASE_URL,
                        onEdit = {
                            serverDraft = viewModel.serverUrl ?: ApiClient.DEFAULT_BASE_URL
                            showServerDialog = true
                        }
                    )

                    Spacer(modifier = Modifier.height(26.dp))
                    LieEyebrow(s.profile.settings)
                    Spacer(modifier = Modifier.height(10.dp))
                    LieCard {
                        SettingRow(
                            title = s.profile.dataSource,
                            value = if (LocalMode.ENABLED) s.profile.localDemo else s.profile.onlineDevice
                        )
                        SettingDivider()
                        SettingRow(
                            title = s.profile.language,
                            value = if (com.aura.android.ui.i18n.I18n.isEnglish()) s.profile.languageEn
                            else s.profile.languageZh,
                            onClick = { showLanguageDialog = true }
                        )
                        SettingDivider()
                        SettingRow(
                            title = s.profile.wallpaper,
                            value = wallpaperLabel(s),
                            onClick = { showWallpaperDialog = true }
                        )
                        SettingDivider()
                        SettingRow(
                            title = s.profile.notifications,
                            value = if (notificationsOn) s.profile.enabled else s.profile.disabled,
                            onClick = { notificationsOn = !notificationsOn }
                        )
                        SettingDivider()
                        SettingRow(title = s.profile.about, value = s.profile.version + " 1.1.4")
                    }

                    Spacer(modifier = Modifier.height(26.dp))
                    LieEyebrow(s.profile.aiApi)
                    Spacer(modifier = Modifier.height(10.dp))
                    LieCard {
                        SettingRow(
                            title = s.profile.model,
                            value = viewModel.aiConfig.model,
                            onClick = {
                                aiBaseDraft = viewModel.aiConfig.baseUrl
                                aiKeyDraft = viewModel.aiConfig.apiKey
                                aiModelDraft = viewModel.aiConfig.model
                                showAiDialog = true
                            }
                        )
                        SettingDivider()
                        SettingRow(
                            title = s.profile.endpoint,
                            value = viewModel.aiConfig.baseUrl
                                .removePrefix("https://")
                                .removePrefix("http://")
                        )
                        SettingDivider()
                        SettingRow(
                            title = s.profile.apiKey,
                            value = viewModel.aiConfig.maskedKey,
                            onClick = {
                                aiBaseDraft = viewModel.aiConfig.baseUrl
                                aiKeyDraft = viewModel.aiConfig.apiKey
                                aiModelDraft = viewModel.aiConfig.model
                                showAiDialog = true
                            }
                        )
                        SettingDivider()
                        SettingRow(
                            title = s.profile.status,
                            value = if (viewModel.aiConfig.isConfigured) s.profile.enabledAnalyze
                            else s.profile.notConfigured
                        )
                    }
                    Text(
                        text = if (com.aura.android.ui.i18n.I18n.isEnglish()) {
                            "Works with any OpenAI-compatible API (DeepSeek / OpenAI / Qwen / " +
                                "local Ollama). The key stays on this device and is only used to " +
                                "send vitals as context."
                        } else {
                            "支持 OpenAI 兼容接口（DeepSeek / OpenAI / 通义千问 / 本地 Ollama）。" +
                                "Key 仅保存在本机，用于把体征数据作为上下文发给模型分析。"
                        },
                        style = MaterialTheme.typography.labelSmall.copy(color = Hint),
                        modifier = Modifier.padding(top = 8.dp, start = 4.dp, end = 4.dp)
                    )

                    Spacer(modifier = Modifier.height(24.dp))
                    Box(
                        modifier = Modifier
                            .fillMaxWidth()
                            .clip(RoundedCornerShape(16.dp))
                            .background(Paper)
                            .clickable { viewModel.logout(onLogout) }
                            .padding(vertical = 15.dp),
                        contentAlignment = Alignment.Center
                    ) {
                        Text(
                            text = s.profile.logout,
                            style = MaterialTheme.typography.labelLarge.copy(
                                color = ErrorRed,
                                fontWeight = FontWeight.SemiBold
                            )
                        )
                    }

                    Text(
                        text = s.profile.footer,
                        style = MaterialTheme.typography.labelSmall.copy(color = Hint),
                        modifier = Modifier.padding(top = 20.dp, bottom = 112.dp)
                    )
                }
                else -> Unit
            }
        }
    }

    if (showServerDialog) {
        AlertDialog(
            onDismissRequest = { showServerDialog = false },
            containerColor = Paper,
            title = { Text("服务器地址", style = MaterialTheme.typography.titleLarge) },
            text = {
                Column {
                    OutlinedTextField(
                        value = serverDraft,
                        onValueChange = { serverDraft = it },
                        singleLine = true,
                        label = { Text("内网或公网地址") },
                        shape = RoundedCornerShape(12.dp),
                        modifier = Modifier.fillMaxWidth()
                    )
                    Spacer(modifier = Modifier.height(8.dp))
                    Text(
                        text = "局域网示例：http://10.119.187.189:3000\n公网示例：https://xxx.trycloudflare.com",
                        style = MaterialTheme.typography.labelSmall.copy(color = Hint)
                    )
                }
            },
            confirmButton = {
                TextButton(onClick = {
                    viewModel.updateServer(serverDraft)
                    showServerDialog = false
                }) {
                    Text("保存", color = PrimaryBlue, fontWeight = FontWeight.Bold)
                }
            },
            dismissButton = {
                TextButton(onClick = { showServerDialog = false }) {
                    Text("取消", color = Muted)
                }
            }
        )
    }

    if (showAiDialog) {
        AiConfigDialog(
            baseUrl = aiBaseDraft,
            apiKey = aiKeyDraft,
            model = aiModelDraft,
            onBaseUrlChange = { aiBaseDraft = it },
            onApiKeyChange = { aiKeyDraft = it },
            onModelChange = { aiModelDraft = it },
            onPreset = { preset ->
                aiBaseDraft = preset.baseUrl
                aiModelDraft = preset.model
            },
            onDismiss = { showAiDialog = false },
            onSave = {
                viewModel.saveAiConfig(aiBaseDraft, aiKeyDraft, aiModelDraft)
                showAiDialog = false
            }
        )
    }

    if (showNameDialog) {
        AlertDialog(
            onDismissRequest = { showNameDialog = false },
            containerColor = Paper,
            title = { Text("修改昵称", style = MaterialTheme.typography.titleLarge) },
            text = {
                Column {
                    OutlinedTextField(
                        value = nameDraft,
                        onValueChange = { nameDraft = it.take(40) },
                        singleLine = true,
                        label = { Text("昵称") },
                        shape = RoundedCornerShape(12.dp),
                        modifier = Modifier.fillMaxWidth()
                    )
                    Spacer(modifier = Modifier.height(8.dp))
                    Text(
                        text = "想换头像？直接点左上角头像，从相册选一张即可。",
                        style = MaterialTheme.typography.labelSmall.copy(color = Hint)
                    )
                }
            },
            confirmButton = {
                TextButton(onClick = {
                    viewModel.updateName(nameDraft)
                    showNameDialog = false
                }) {
                    Text("保存", color = PrimaryBlue, fontWeight = FontWeight.Bold)
                }
            },
            dismissButton = {
                TextButton(onClick = { showNameDialog = false }) {
                    Text("取消", color = Muted)
                }
            }
        )
    }
    if (showLanguageDialog) {
        AlertDialog(
            onDismissRequest = { showLanguageDialog = false },
            containerColor = Paper,
            title = { Text(s.profile.language, style = MaterialTheme.typography.titleLarge) },
            text = {
                Column {
                    SelectOption(
                        label = s.profile.languageZh,
                        selected = I18n.language == AppLanguage.ZH
                    ) {
                        viewModel.setLanguage(AppLanguage.ZH)
                        showLanguageDialog = false
                    }
                    SelectOption(
                        label = s.profile.languageEn,
                        selected = I18n.language == AppLanguage.EN
                    ) {
                        viewModel.setLanguage(AppLanguage.EN)
                        showLanguageDialog = false
                    }
                }
            },
            confirmButton = {
                TextButton(onClick = { showLanguageDialog = false }) {
                    Text(s.common.cancel, color = Muted)
                }
            }
        )
    }

    if (showWallpaperDialog) {
        AlertDialog(
            onDismissRequest = { showWallpaperDialog = false },
            containerColor = Paper,
            title = { Text(s.profile.wallpaper, style = MaterialTheme.typography.titleLarge) },
            text = {
                Column(modifier = Modifier.verticalScroll(rememberScrollState())) {
                    val current = Wallpaper.preset
                    WallpaperOption(s.profile.wallpaperDefault, current == WallpaperPreset.DEFAULT) {
                        viewModel.setWallpaperPreset(WallpaperPreset.DEFAULT)
                        showWallpaperDialog = false
                    }
                    WallpaperOption(s.profile.wallpaperPreset1, current == WallpaperPreset.WARM) {
                        viewModel.setWallpaperPreset(WallpaperPreset.WARM)
                        showWallpaperDialog = false
                    }
                    WallpaperOption(s.profile.wallpaperPreset2, current == WallpaperPreset.MIST) {
                        viewModel.setWallpaperPreset(WallpaperPreset.MIST)
                        showWallpaperDialog = false
                    }
                    WallpaperOption(s.profile.wallpaperPreset3, current == WallpaperPreset.DEEP) {
                        viewModel.setWallpaperPreset(WallpaperPreset.DEEP)
                        showWallpaperDialog = false
                    }
                    WallpaperOption(s.profile.wallpaperPreset4, current == WallpaperPreset.MORNING) {
                        viewModel.setWallpaperPreset(WallpaperPreset.MORNING)
                        showWallpaperDialog = false
                    }
                    SelectOption(
                        label = s.profile.wallpaperPick,
                        selected = current == WallpaperPreset.CUSTOM
                    ) {
                        pickWallpaper.launch(
                            PickVisualMediaRequest(ActivityResultContracts.PickVisualMedia.ImageOnly)
                        )
                    }
                }
            },
            confirmButton = {
                TextButton(onClick = { showWallpaperDialog = false }) {
                    Text(s.common.cancel, color = Muted)
                }
            }
        )
    }
}

@Composable
private fun SelectOption(label: String, selected: Boolean, onClick: () -> Unit) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(12.dp))
            .clickable(onClick = onClick)
            .padding(horizontal = 8.dp, vertical = 14.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Text(
            text = label,
            style = MaterialTheme.typography.bodyLarge.copy(color = OnSurface),
            modifier = Modifier.weight(1f)
        )
        if (selected) {
            Text(
                text = "✓",
                style = MaterialTheme.typography.titleMedium.copy(
                    color = PrimaryBlue,
                    fontWeight = FontWeight.Bold
                )
            )
        }
    }
}

// 壁纸选项
@Composable
private fun WallpaperOption(label: String, selected: Boolean, onClick: () -> Unit) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(12.dp))
            .clickable(onClick = onClick)
            .padding(horizontal = 8.dp, vertical = 12.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Box(
            modifier = Modifier
                .size(28.dp)
                .clip(RoundedCornerShape(8.dp))
                .background(wallpaperBrush(label))
        )
        Spacer(modifier = Modifier.width(12.dp))
        Text(
            text = label,
            style = MaterialTheme.typography.bodyLarge.copy(color = OnSurface),
            modifier = Modifier.weight(1f)
        )
        if (selected) {
            Text(
                text = "✓",
                style = MaterialTheme.typography.titleMedium.copy(
                    color = PrimaryBlue,
                    fontWeight = FontWeight.Bold
                )
            )
        }
    }
}

private fun wallpaperBrush(label: String): Brush = when (label) {
    "暖砂", "Warm sand" -> Brush.verticalGradient(listOf(Color(0xFFF8F1E8), Color(0xFFE7D9C7)))
    "青雾", "Mist" -> Brush.verticalGradient(listOf(Color(0xFFEEF2F0), Color(0xFFD5E0DA)))
    "深海", "Deep sea" -> Brush.verticalGradient(listOf(Color(0xFFE9EDF3), Color(0xFFC9D4E2)))
    "晨光", "Morning" -> Brush.verticalGradient(listOf(Color(0xFFFFF4E9), Color(0xFFF2D6C2)))
    else -> Brush.verticalGradient(listOf(Surface, Surface))
}

@Composable
private fun wallpaperLabel(s: Strings): String = when (Wallpaper.preset) {
    WallpaperPreset.DEFAULT -> s.profile.wallpaperDefault
    WallpaperPreset.WARM -> s.profile.wallpaperPreset1
    WallpaperPreset.MIST -> s.profile.wallpaperPreset2
    WallpaperPreset.DEEP -> s.profile.wallpaperPreset3
    WallpaperPreset.MORNING -> s.profile.wallpaperPreset4
    WallpaperPreset.CUSTOM -> s.profile.wallpaperCustom
}

// 压缩头像并转 Base64
private suspend fun encodeAvatar(context: android.content.Context, uri: Uri): String? =
    withContext(Dispatchers.IO) {
        try {
            val resolver = context.contentResolver
            val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
            resolver.openInputStream(uri)?.use { BitmapFactory.decodeStream(it, null, bounds) }
            val width = bounds.outWidth
            val height = bounds.outHeight
            if (width <= 0 || height <= 0) return@withContext null

            var sample = 1
            while (width / (sample * 2) >= AVATAR_MAX_SIZE &&
                height / (sample * 2) >= AVATAR_MAX_SIZE
            ) {
                sample *= 2
            }
            val options = BitmapFactory.Options().apply { inSampleSize = sample }
            val decoded = resolver.openInputStream(uri)?.use {
                BitmapFactory.decodeStream(it, null, options)
            } ?: return@withContext null

            val scale = AVATAR_MAX_SIZE.toFloat() / maxOf(decoded.width, decoded.height)
            val scaled: Bitmap = if (scale >= 1f) decoded else Bitmap.createScaledBitmap(
                decoded,
                (decoded.width * scale).toInt().coerceAtLeast(1),
                (decoded.height * scale).toInt().coerceAtLeast(1),
                true
            )
            val output = ByteArrayOutputStream()
            scaled.compress(Bitmap.CompressFormat.JPEG, 82, output)
            Base64.encodeToString(output.toByteArray(), Base64.NO_WRAP)
        } catch (e: Exception) {
            null
        }
    }

private const val AVATAR_MAX_SIZE = 256

// AI 接口配置弹窗
@Composable
private fun AiConfigDialog(
    baseUrl: String,
    apiKey: String,
    model: String,
    onBaseUrlChange: (String) -> Unit,
    onApiKeyChange: (String) -> Unit,
    onModelChange: (String) -> Unit,
    onPreset: (com.aura.android.data.ai.AiPreset) -> Unit,
    onDismiss: () -> Unit,
    onSave: () -> Unit
) {
    var keyVisible by remember { mutableStateOf(false) }

    AlertDialog(
        onDismissRequest = onDismiss,
        containerColor = Paper,
        title = { Text("AI 接口配置", style = MaterialTheme.typography.titleLarge) },
        text = {
            Column(modifier = Modifier.verticalScroll(rememberScrollState())) {
                Text(
                    text = "预设",
                    style = MaterialTheme.typography.labelSmall.copy(color = Hint)
                )
                Spacer(modifier = Modifier.height(6.dp))
                Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                    AiConfig.PRESETS.take(2).forEach { preset ->
                        PresetChip(preset.label, onClick = { onPreset(preset) })
                    }
                }
                Spacer(modifier = Modifier.height(6.dp))
                Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                    AiConfig.PRESETS.drop(2).forEach { preset ->
                        PresetChip(preset.label, onClick = { onPreset(preset) })
                    }
                }

                Spacer(modifier = Modifier.height(12.dp))
                OutlinedTextField(
                    value = baseUrl,
                    onValueChange = onBaseUrlChange,
                    singleLine = true,
                    label = { Text("接口地址（到 /v1）") },
                    placeholder = { Text("https://api.deepseek.com/v1") },
                    shape = RoundedCornerShape(12.dp),
                    modifier = Modifier.fillMaxWidth()
                )
                Spacer(modifier = Modifier.height(8.dp))
                OutlinedTextField(
                    value = apiKey,
                    onValueChange = onApiKeyChange,
                    singleLine = true,
                    label = { Text("API Key") },
                    placeholder = { Text("sk-...") },
                    visualTransformation = if (keyVisible) VisualTransformation.None
                    else PasswordVisualTransformation(),
                    trailingIcon = {
                        IconButton(onClick = { keyVisible = !keyVisible }) {
                            Icon(
                                imageVector = if (keyVisible) Icons.Default.VisibilityOff
                                else Icons.Default.Visibility,
                                contentDescription = if (keyVisible) "隐藏" else "显示",
                                tint = Muted
                            )
                        }
                    },
                    shape = RoundedCornerShape(12.dp),
                    modifier = Modifier.fillMaxWidth()
                )
                Spacer(modifier = Modifier.height(8.dp))
                OutlinedTextField(
                    value = model,
                    onValueChange = onModelChange,
                    singleLine = true,
                    label = { Text("模型名称") },
                    placeholder = { Text("deepseek-chat") },
                    shape = RoundedCornerShape(12.dp),
                    modifier = Modifier.fillMaxWidth()
                )
                Spacer(modifier = Modifier.height(8.dp))
                Text(
                    text = "保存后，AI 页会把当前心率、呼吸、专注、压力、疲劳与告警摘要作为上下文发送给该模型。",
                    style = MaterialTheme.typography.labelSmall.copy(color = Hint)
                )
            }
        },
        confirmButton = {
            TextButton(onClick = onSave) {
                Text("保存", color = PrimaryBlue, fontWeight = FontWeight.Bold)
            }
        },
        dismissButton = {
            TextButton(onClick = onDismiss) {
                Text("取消", color = Muted)
            }
        }
    )
}

@Composable
private fun PresetChip(label: String, onClick: () -> Unit) {
    Box(
        modifier = Modifier
            .clip(RoundedCornerShape(50))
            .background(SecondaryContainer)
            .clickable(onClick = onClick)
            .padding(horizontal = 10.dp, vertical = 6.dp)
    ) {
        Text(
            text = label,
            style = MaterialTheme.typography.labelSmall.copy(color = OnSurface, fontSize = 11.sp)
        )
    }
}

@Composable
private fun ServerCard(s: Strings, url: String, onEdit: () -> Unit) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(16.dp))
            .background(Paper)
            .border(1.dp, Hairline, RoundedCornerShape(16.dp))
            .clickable(onClick = onEdit)
            .padding(16.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Box(
            modifier = Modifier
                .size(32.dp)
                .clip(RoundedCornerShape(10.dp))
                .background(SecondaryContainer),
            contentAlignment = Alignment.Center
        ) {
            Text(
                text = "服",
                style = MaterialTheme.typography.labelMedium.copy(
                    color = PrimaryBlue,
                    fontWeight = FontWeight.Bold
                )
            )
        }
        Spacer(modifier = Modifier.width(12.dp))
        Column(modifier = Modifier.weight(1f)) {
            Text(
                text = s.profile.server,
                style = MaterialTheme.typography.titleMedium.copy(
                    color = OnSurface,
                    fontSize = 15.sp
                )
            )
            Spacer(modifier = Modifier.height(2.dp))
            Text(
                text = url,
                style = MaterialTheme.typography.labelMedium.copy(color = Muted),
                maxLines = 1
            )
        }
        Text(
            text = s.profile.modify,
            style = MaterialTheme.typography.labelSmall.copy(color = PrimaryBlue)
        )
    }
}

@Composable
private fun ProfileNav(s: Strings, saving: Boolean, onRefresh: () -> Unit) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .background(Color.Transparent)
            .statusBarsPadding()
            .padding(horizontal = 24.dp, vertical = 10.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Text(
            text = s.profile.title,
            style = MaterialTheme.typography.titleLarge.copy(color = OnSurface)
        )
        Spacer(modifier = Modifier.weight(1f))
        if (saving) {
            CircularProgressIndicator(
                modifier = Modifier.size(20.dp),
                color = PrimaryBlue,
                strokeWidth = 2.dp
            )
            Spacer(modifier = Modifier.width(12.dp))
        }
        IconButton(onClick = onRefresh) {
            Icon(Icons.Default.Refresh, contentDescription = s.common.refresh, tint = OnSurface)
        }
    }
}

@Composable
private fun ProfileCard(
    s: Strings,
    user: User,
    onPickAvatar: () -> Unit,
    onEditName: () -> Unit
) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(top = 20.dp, bottom = 22.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Box(
            modifier = Modifier
                .size(64.dp)
                .clip(RoundedCornerShape(24.dp))
                .clickable(onClick = onPickAvatar)
        ) {
            AvatarContent(user = user, size = 64.dp)
            // 更换头像角标
            Box(
                modifier = Modifier
                    .align(Alignment.BottomEnd)
                    .size(22.dp)
                    .clip(RoundedCornerShape(11.dp))
                    .background(PrimaryBlue),
                contentAlignment = Alignment.Center
            ) {
                Text(
                    text = "改",
                    style = MaterialTheme.typography.labelSmall.copy(
                        color = Color.White,
                        fontSize = 10.sp,
                        fontWeight = FontWeight.Bold
                    )
                )
            }
        }
        Spacer(modifier = Modifier.width(16.dp))
        Column(modifier = Modifier.weight(1f)) {
            Row(
                verticalAlignment = Alignment.CenterVertically,
                modifier = Modifier.clickable(onClick = onEditName)
            ) {
                Text(
                    text = user.name,
                    style = MaterialTheme.typography.headlineMedium.copy(color = OnSurface),
                    maxLines = 1
                )
                Spacer(modifier = Modifier.width(8.dp))
                Text(
                    text = s.common.edit,
                    style = MaterialTheme.typography.labelSmall.copy(color = PrimaryBlue)
                )
            }
            Spacer(modifier = Modifier.height(4.dp))
            Text(
                text = user.email,
                style = MaterialTheme.typography.bodyMedium.copy(color = Muted)
            )
        }
    }
}

// 头像内容
@Composable
private fun AvatarContent(user: User, size: Dp) {
    val bitmap = remember(user.avatar) {
        user.avatar?.let { raw ->
            runCatching {
                val bytes = Base64.decode(raw, Base64.DEFAULT)
                BitmapFactory.decodeByteArray(bytes, 0, bytes.size)?.asImageBitmap()
            }.getOrNull()
        }
    }
    if (bitmap != null) {
        Image(
            bitmap = bitmap,
            contentDescription = "头像",
            contentScale = ContentScale.Crop,
            modifier = Modifier
                .size(size)
                .clip(RoundedCornerShape(24.dp))
        )
    } else {
        Box(
            modifier = Modifier
                .size(size)
                .clip(RoundedCornerShape(24.dp))
                .background(PrimaryBlue),
            contentAlignment = Alignment.Center
        ) {
            Text(
                text = user.name.take(1),
                style = MaterialTheme.typography.headlineMedium.copy(
                    color = Color.White,
                    fontWeight = FontWeight.Bold
                )
            )
        }
    }
}

@Composable
private fun SettingRow(
    title: String,
    value: String,
    onClick: (() -> Unit)? = null
) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .then(if (onClick != null) Modifier.clickable(onClick = onClick) else Modifier)
            .padding(vertical = 14.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Text(
            text = title,
            style = MaterialTheme.typography.bodyLarge,
            color = OnSurface,
            modifier = Modifier.weight(1f)
        )
        Text(
            text = value,
            style = MaterialTheme.typography.labelMedium,
            color = Hint
        )
        if (onClick != null) {
            Spacer(modifier = Modifier.width(6.dp))
            Text(
                text = "›",
                style = MaterialTheme.typography.titleLarge,
                color = Hint
            )
        }
    }
}

@Composable
private fun SettingDivider() {
    Box(
        modifier = Modifier
            .fillMaxWidth()
            .height(1.dp)
            .background(Hairline.copy(alpha = 0.55f))
    )
}

// 格式化日期
private fun formatDay(raw: String): String = try {
    java.time.format.DateTimeFormatter
        .ofPattern("yyyy-MM-dd")
        .withZone(java.time.ZoneId.systemDefault())
        .format(java.time.Instant.parse(raw))
} catch (e: Exception) {
    raw.take(10)
}
