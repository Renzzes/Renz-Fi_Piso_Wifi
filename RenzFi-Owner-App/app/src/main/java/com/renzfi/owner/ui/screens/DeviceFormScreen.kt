package com.renzfi.owner.ui.screens

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.renzfi.owner.ui.components.RenzFiTopBar
import com.renzfi.owner.util.Constants
import com.renzfi.owner.viewmodel.DeviceFormUiState

@Composable
fun DeviceFormScreen(
    uiState: DeviceFormUiState,
    isEditMode: Boolean,
    onNameChange: (String) -> Unit,
    onMikrotikDisplayNameChange: (String) -> Unit,
    onMikrotikDdnsChange: (String) -> Unit,
    onMikrotikPublicIpChange: (String) -> Unit,
    onMikrotikNotesChange: (String) -> Unit,
    onEsp32LocalIpChange: (String) -> Unit,
    onDiscoverySubnetChange: (String) -> Unit,
    onDiscoverSubnet: () -> Unit,
    onDiscoverAtIp: () -> Unit,
    onSave: () -> Unit,
    onBack: () -> Unit,
    onSaveSuccessShown: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val snackbarHostState = remember { SnackbarHostState() }

    LaunchedEffect(uiState.saveSuccess) {
        if (uiState.saveSuccess) {
            snackbarHostState.showSnackbar(if (isEditMode) "Device updated" else "Device added")
            onSaveSuccessShown()
            onBack()
        }
    }

    LaunchedEffect(uiState.errorMessage) {
        uiState.errorMessage?.let { snackbarHostState.showSnackbar(it) }
    }

    Scaffold(
        modifier = modifier,
        topBar = {
            RenzFiTopBar(
                title = if (isEditMode) "Edit Device" else "Add Device",
                onBack = onBack,
            )
        },
        snackbarHost = { SnackbarHost(snackbarHostState) },
    ) { padding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding)
                .padding(24.dp)
                .verticalScroll(rememberScrollState()),
        ) {
            Text(
                text = "Device Information",
                style = MaterialTheme.typography.titleLarge,
            )
            Spacer(modifier = Modifier.height(16.dp))
            OutlinedTextField(
                value = uiState.name,
                onValueChange = onNameChange,
                modifier = Modifier.fillMaxWidth(),
                label = { Text("Device Name") },
                placeholder = { Text("Main Branch") },
                singleLine = true,
                enabled = !uiState.isSaving,
            )
            Spacer(modifier = Modifier.height(24.dp))
            Text(
                text = "MikroTik (future WireGuard VPN)",
                style = MaterialTheme.typography.titleMedium,
            )
            Spacer(modifier = Modifier.height(4.dp))
            Text(
                text = "Stored for future remote access via MikroTik WireGuard. Not used for local dashboard access.",
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.6f),
            )
            Spacer(modifier = Modifier.height(12.dp))
            OutlinedTextField(
                value = uiState.mikrotikDisplayName,
                onValueChange = onMikrotikDisplayNameChange,
                modifier = Modifier.fillMaxWidth(),
                label = { Text("Display Name") },
                singleLine = true,
                enabled = !uiState.isSaving,
            )
            Spacer(modifier = Modifier.height(12.dp))
            OutlinedTextField(
                value = uiState.mikrotikDdns,
                onValueChange = onMikrotikDdnsChange,
                modifier = Modifier.fillMaxWidth(),
                label = { Text("DDNS Hostname") },
                placeholder = { Text("abc123.sn.mynetname.net") },
                singleLine = true,
                enabled = !uiState.isSaving,
            )
            Spacer(modifier = Modifier.height(12.dp))
            OutlinedTextField(
                value = uiState.mikrotikPublicIp,
                onValueChange = onMikrotikPublicIpChange,
                modifier = Modifier.fillMaxWidth(),
                label = { Text("Public IP (optional)") },
                singleLine = true,
                enabled = !uiState.isSaving,
            )
            Spacer(modifier = Modifier.height(12.dp))
            OutlinedTextField(
                value = uiState.mikrotikNotes,
                onValueChange = onMikrotikNotesChange,
                modifier = Modifier.fillMaxWidth(),
                label = { Text("Notes") },
                minLines = 2,
                enabled = !uiState.isSaving,
            )
            Spacer(modifier = Modifier.height(24.dp))
            Text(
                text = "Device Registry",
                style = MaterialTheme.typography.titleMedium,
            )
            Spacer(modifier = Modifier.height(4.dp))
            Text(
                text = "Discover appliances on your LAN — no manual IP required.",
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.6f),
            )
            Spacer(modifier = Modifier.height(12.dp))
            OutlinedTextField(
                value = uiState.discoverySubnet,
                onValueChange = onDiscoverySubnetChange,
                modifier = Modifier.fillMaxWidth(),
                label = { Text("Discovery subnet") },
                placeholder = { Text(Constants.DEFAULT_DISCOVERY_SUBNET) },
                singleLine = true,
                enabled = !uiState.isSaving && !uiState.isDiscovering,
            )
            Spacer(modifier = Modifier.height(12.dp))
            Button(
                onClick = onDiscoverSubnet,
                modifier = Modifier.fillMaxWidth(),
                enabled = !uiState.isSaving && !uiState.isDiscovering,
            ) {
                if (uiState.isDiscovering) {
                    CircularProgressIndicator(
                        modifier = Modifier.height(20.dp),
                        strokeWidth = 2.dp,
                    )
                } else {
                    Text("Discover on subnet")
                }
            }
            uiState.discoveryMessage?.let { message ->
                Spacer(modifier = Modifier.height(8.dp))
                Text(
                    text = message,
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.primary,
                )
            }
            Spacer(modifier = Modifier.height(24.dp))
            Text(
                text = "ESP32 Appliance",
                style = MaterialTheme.typography.titleMedium,
            )
            Spacer(modifier = Modifier.height(12.dp))
            OutlinedTextField(
                value = uiState.esp32LocalIp,
                onValueChange = onEsp32LocalIpChange,
                modifier = Modifier.fillMaxWidth(),
                label = { Text("ESP32 Local IP (optional override)") },
                placeholder = { Text(Constants.DEFAULT_ESP32_IP) },
                singleLine = true,
                enabled = !uiState.isSaving && !uiState.isDiscovering,
            )
            Spacer(modifier = Modifier.height(12.dp))
            Button(
                onClick = onDiscoverAtIp,
                modifier = Modifier.fillMaxWidth(),
                enabled = !uiState.isSaving && !uiState.isDiscovering,
            ) {
                Text("Probe IP")
            }
            Spacer(modifier = Modifier.height(24.dp))
            Button(
                onClick = onSave,
                modifier = Modifier.fillMaxWidth(),
                enabled = !uiState.isSaving,
            ) {
                if (uiState.isSaving) {
                    CircularProgressIndicator(
                        modifier = Modifier.height(20.dp),
                        strokeWidth = 2.dp,
                    )
                } else {
                    Text(if (isEditMode) "Save Changes" else "Add Device")
                }
            }
        }
    }
}
