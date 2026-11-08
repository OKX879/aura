package com.aura.android.ui.components

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.aura.android.data.DiscoveredDevice
import com.aura.android.data.model.Device
import com.aura.android.ui.i18n.Strings
import com.aura.android.ui.theme.ErrorRed
import com.aura.android.ui.theme.Hint
import com.aura.android.ui.theme.Muted
import com.aura.android.ui.theme.Paper
import com.aura.android.ui.theme.PrimaryBlue
import com.aura.android.ui.theme.SecondaryContainer
import kotlinx.coroutines.launch

// 添加雷达设备对话框
@Composable
fun AddDeviceDialog(
    defaultEndpoint: String,
    discovered: List<DiscoveredDevice>,
    scanning: Boolean,
    onScan: () -> Unit,
    onDismiss: () -> Unit,
    onSubmit: suspend (name: String, endpointUrl: String, code: String) -> Result<Device>,
    s: Strings
) {
    var name by remember { mutableStateOf("") }
    var endpoint by remember { mutableStateOf(defaultEndpoint) }
    var code by remember { mutableStateOf("") }
    var submitting by remember { mutableStateOf(false) }
    var error by remember { mutableStateOf<String?>(null) }
    val scope = rememberCoroutineScope()

    AlertDialog(
        onDismissRequest = { if (!submitting) onDismiss() },
        containerColor = Paper,
        title = { Text(s.devices.addTitle, style = MaterialTheme.typography.titleLarge) },
        text = {
            Column(modifier = Modifier.verticalScroll(rememberScrollState())) {
                // 扫描区
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.SpaceBetween
                ) {
                    Text(
                        text = s.devices.scan,
                        style = MaterialTheme.typography.labelMedium.copy(color = PrimaryBlue),
                        modifier = Modifier
                            .clickable(enabled = !scanning && !submitting) { onScan() }
                            .padding(vertical = 6.dp)
                    )
                    if (scanning) {
                        CircularProgressIndicator(
                            color = PrimaryBlue,
                            strokeWidth = 2.dp,
                            modifier = Modifier.height(16.dp)
                        )
                    }
                }
                if (discovered.isNotEmpty()) {
                    discovered.forEach { device ->
                        Row(
                            modifier = Modifier
                                .fillMaxWidth()
                                .padding(vertical = 4.dp)
                                .clickable {
                                    name = device.name
                                    endpoint = device.endpoint
                                }
                                .padding(horizontal = 8.dp, vertical = 10.dp),
                            verticalAlignment = Alignment.CenterVertically
                        ) {
                            Column(modifier = Modifier.weight(1f)) {
                                Text(
                                    text = device.name.ifBlank { device.serial },
                                    style = MaterialTheme.typography.bodyLarge
                                )
                                Text(
                                    text = "${device.ip} · ${device.serial}",
                                    style = MaterialTheme.typography.labelSmall.copy(color = Hint)
                                )
                            }
                            Text(
                                text = "›",
                                style = MaterialTheme.typography.titleLarge.copy(color = Hint)
                            )
                        }
                    }
                    Spacer(modifier = Modifier.height(4.dp))
                } else if (!scanning) {
                    Text(
                        text = s.devices.noDeviceFound,
                        style = MaterialTheme.typography.labelSmall.copy(color = Hint)
                    )
                }

                Spacer(modifier = Modifier.height(10.dp))
                OutlinedTextField(
                    value = name,
                    onValueChange = { name = it },
                    singleLine = true,
                    label = { Text(s.devices.name) },
                    placeholder = { Text(s.devices.nameHint) },
                    shape = RoundedCornerShape(12.dp),
                    modifier = Modifier.fillMaxWidth()
                )
                Spacer(modifier = Modifier.height(8.dp))
                OutlinedTextField(
                    value = endpoint,
                    onValueChange = { endpoint = it },
                    singleLine = true,
                    label = { Text(s.devices.addressLabel) },
                    placeholder = { Text(s.devices.addressHint) },
                    shape = RoundedCornerShape(12.dp),
                    modifier = Modifier.fillMaxWidth()
                )
                Spacer(modifier = Modifier.height(8.dp))
                OutlinedTextField(
                    value = code,
                    onValueChange = { code = it.filter(Char::isDigit).take(6) },
                    singleLine = true,
                    label = { Text(s.devices.codeLabel) },
                    placeholder = { Text("123456") },
                    shape = RoundedCornerShape(12.dp),
                    modifier = Modifier.fillMaxWidth()
                )
                Spacer(modifier = Modifier.height(10.dp))
                Text(
                    text = s.devices.registerHint,
                    style = MaterialTheme.typography.labelSmall.copy(color = Hint)
                )
                if (error != null) {
                    Spacer(modifier = Modifier.height(8.dp))
                    Text(
                        text = error ?: "",
                        style = MaterialTheme.typography.labelSmall.copy(color = ErrorRed)
                    )
                }
            }
        },
        confirmButton = {
            TextButton(
                enabled = !submitting && name.isNotBlank() &&
                    endpoint.isNotBlank() && code.length == 6,
                onClick = {
                    error = null
                    submitting = true
                    scope.launch {
                        val result = onSubmit(name.trim(), endpoint.trim(), code.trim())
                        submitting = false
                        result.fold(
                            onSuccess = { onDismiss() },
                            onFailure = { error = it.message ?: s.devices.pairFailed }
                        )
                    }
                }
            ) {
                if (submitting) {
                    CircularProgressIndicator(
                        color = PrimaryBlue,
                        strokeWidth = 2.dp,
                        modifier = Modifier.height(16.dp)
                    )
                } else {
                    Text(
                        text = s.devices.pairVerify,
                        color = PrimaryBlue,
                        fontWeight = FontWeight.Bold
                    )
                }
            }
        },
        dismissButton = {
            TextButton(onClick = onDismiss, enabled = !submitting) {
                Text(s.common.cancel, color = Muted)
            }
        }
    )
}
