package com.renzfi.owner.ui.screens

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowForward
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.Info
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import com.renzfi.owner.R
import com.renzfi.owner.model.VendoDevice
import com.renzfi.owner.ui.components.DeleteConfirmDialog
import com.renzfi.owner.ui.components.OnlineStatusBadge
import com.renzfi.owner.ui.components.RenzFiTopBar
import com.renzfi.owner.viewmodel.SettingsUiState

@Composable
fun SettingsScreen(
    uiState: SettingsUiState,
    onAddDevice: () -> Unit,
    onEditDevice: (VendoDevice) -> Unit,
    onDeleteDevice: (VendoDevice) -> Unit,
    onAbout: () -> Unit,
    onBack: () -> Unit,
    modifier: Modifier = Modifier,
) {
    var deviceToDelete by remember { mutableStateOf<VendoDevice?>(null) }

    if (deviceToDelete != null) {
        DeleteConfirmDialog(
            deviceName = deviceToDelete!!.name,
            onConfirm = {
                onDeleteDevice(deviceToDelete!!)
                deviceToDelete = null
            },
            onDismiss = { deviceToDelete = null },
        )
    }

    Scaffold(
        modifier = modifier,
        topBar = {
            RenzFiTopBar(title = stringResource(R.string.settings_title), onBack = onBack)
        },
    ) { padding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding)
                .padding(24.dp)
                .verticalScroll(rememberScrollState()),
        ) {
            Text(
                text = stringResource(R.string.device_management),
                style = MaterialTheme.typography.titleLarge,
            )
            Spacer(modifier = Modifier.height(8.dp))
            Text(
                text = "Add, edit, or remove vendo installations. Each device stores its ESP32 local IP and MikroTik details for future VPN access.",
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.7f),
            )
            Spacer(modifier = Modifier.height(16.dp))
            OutlinedButton(
                onClick = onAddDevice,
                modifier = Modifier.fillMaxWidth(),
            ) {
                Icon(Icons.Default.Add, contentDescription = null)
                Text(
                    text = "Add Device",
                    modifier = Modifier.padding(start = 8.dp),
                )
            }
            Spacer(modifier = Modifier.height(16.dp))

            if (uiState.devices.isEmpty()) {
                Text(
                    text = "No devices configured.",
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.6f),
                )
            } else {
                uiState.devices.forEach { device ->
                    SettingsDeviceItem(
                        device = device,
                        onEdit = { onEditDevice(device) },
                        onDelete = { deviceToDelete = device },
                    )
                    Spacer(modifier = Modifier.height(8.dp))
                }
            }

            Spacer(modifier = Modifier.height(24.dp))
            HorizontalDivider()
            Spacer(modifier = Modifier.height(16.dp))
            TextButton(
                onClick = onAbout,
                modifier = Modifier.fillMaxWidth(),
            ) {
                Icon(Icons.Default.Info, contentDescription = null)
                Text(
                    text = stringResource(R.string.about_link_label),
                    modifier = Modifier.padding(start = 8.dp),
                )
                Spacer(modifier = Modifier.weight(1f))
                Icon(Icons.AutoMirrored.Filled.ArrowForward, contentDescription = null)
            }
        }
    }
}

@Composable
private fun SettingsDeviceItem(
    device: VendoDevice,
    onEdit: () -> Unit,
    onDelete: () -> Unit,
) {
    Card(
        modifier = Modifier.fillMaxWidth(),
        colors = CardDefaults.cardColors(
            containerColor = MaterialTheme.colorScheme.surfaceVariant,
        ),
    ) {
        Column(modifier = Modifier.padding(16.dp)) {
            Text(
                text = device.name,
                style = MaterialTheme.typography.titleMedium,
                fontWeight = FontWeight.Bold,
            )
            Text(
                text = device.esp32LocalIp,
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.7f),
            )
            OnlineStatusBadge(device = device)
            Spacer(modifier = Modifier.height(8.dp))
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.End,
            ) {
                TextButton(onClick = onEdit) {
                    Text("Edit")
                }
                TextButton(onClick = onDelete) {
                    Text("Delete", color = MaterialTheme.colorScheme.error)
                }
            }
        }
    }
}
