package com.renzfi.owner.ui.screens

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.ExtendedFloatingActionButton
import androidx.compose.material3.FloatingActionButton
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import com.renzfi.owner.model.VendoDevice
import com.renzfi.owner.ui.components.DeleteConfirmDialog
import com.renzfi.owner.ui.components.DeviceCard
import com.renzfi.owner.ui.components.RenzFiTopBar
import com.renzfi.owner.viewmodel.DeviceListUiState

/**
 * "My Vendo" — main home screen shown whenever at least one appliance is registered.
 * Lists all saved appliances with name, IP, online status, and a "Login to Admin" button.
 */
@Composable
fun DeviceListScreen(
    uiState: DeviceListUiState,
    onRefresh: () -> Unit,
    onLoginToAdmin: (VendoDevice) -> Unit,
    onEditDevice: (VendoDevice) -> Unit,
    onDeleteDevice: (VendoDevice) -> Unit,
    onAddDevice: () -> Unit,
    onOpenSettings: () -> Unit,
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
            RenzFiTopBar(
                title = "My Vendo",
                actions = {
                    IconButton(onClick = onRefresh, enabled = !uiState.isRefreshing) {
                        Icon(
                            imageVector = Icons.Default.Refresh,
                            contentDescription = "Refresh status",
                            tint = Color.White,
                        )
                    }
                    IconButton(onClick = onOpenSettings) {
                        Icon(
                            imageVector = Icons.Default.Settings,
                            contentDescription = "Settings",
                            tint = Color.White,
                        )
                    }
                },
            )
        },
        floatingActionButton = {
            if (uiState.devices.isEmpty()) {
                ExtendedFloatingActionButton(
                    onClick = onAddDevice,
                    icon = { Icon(Icons.Default.Add, contentDescription = null) },
                    text = { Text("Add Appliance") },
                )
            } else {
                FloatingActionButton(onClick = onAddDevice) {
                    Icon(Icons.Default.Add, contentDescription = "Add appliance")
                }
            }
        },
    ) { padding ->
        Box(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding),
        ) {
            if (uiState.devices.isEmpty()) {
                Text(
                    text = "No appliances registered.\nTap + to add your Renz-Fi appliance.",
                    style = MaterialTheme.typography.bodyLarge,
                    color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.7f),
                    modifier = Modifier
                        .align(Alignment.Center)
                        .padding(32.dp)
                        .fillMaxWidth(),
                    textAlign = androidx.compose.ui.text.style.TextAlign.Center,
                )
            } else {
                LazyColumn(
                    contentPadding = PaddingValues(16.dp),
                    verticalArrangement = Arrangement.spacedBy(12.dp),
                ) {
                    items(uiState.devices, key = { it.id }) { device ->
                        DeviceCard(
                            device = device,
                            onLoginToAdmin = { onLoginToAdmin(device) },
                            onEdit = { onEditDevice(device) },
                            onDelete = { deviceToDelete = device },
                        )
                    }
                }
            }

            if (uiState.isRefreshing) {
                CircularProgressIndicator(
                    modifier = Modifier
                        .align(Alignment.TopCenter)
                        .padding(top = 8.dp),
                )
            }
        }
    }
}
