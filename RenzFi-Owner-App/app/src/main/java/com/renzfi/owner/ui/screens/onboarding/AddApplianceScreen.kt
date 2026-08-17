package com.renzfi.owner.ui.screens.onboarding

import android.content.Intent
import android.provider.Settings
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.Search
import androidx.compose.material.icons.filled.Wifi
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import com.renzfi.owner.util.Constants
import com.renzfi.owner.util.ManagementApNetworkUtils
import com.renzfi.owner.viewmodel.AddApplianceScanStatus
import com.renzfi.owner.viewmodel.AddApplianceUiState

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun AddApplianceScreen(
    uiState: AddApplianceUiState,
    onScanNearby: () -> Unit,
    onOpenWifiSettings: () -> Unit,
    onAddExisting: () -> Unit,
    onBack: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val context = LocalContext.current
    val isScanning = uiState.scanStatus == AddApplianceScanStatus.Scanning

    Scaffold(
        modifier = modifier,
        topBar = {
            TopAppBar(
                title = { Text("Add appliance") },
                navigationIcon = {
                    IconButton(onClick = onBack) {
                        Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = "Back")
                    }
                },
            )
        },
    ) { padding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding)
                .padding(24.dp),
            verticalArrangement = Arrangement.spacedBy(16.dp),
        ) {
            when (uiState.scanStatus) {
                AddApplianceScanStatus.Scanning -> {
                    Column(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalAlignment = Alignment.CenterHorizontally,
                        verticalArrangement = Arrangement.spacedBy(16.dp),
                    ) {
                        CircularProgressIndicator()
                        Text("Scanning nearby…", style = MaterialTheme.typography.bodyLarge)
                        Text(
                            "Checking for a Renz-Fi setup appliance on ${Constants.MANAGEMENT_AP_IP}.",
                            style = MaterialTheme.typography.bodyMedium,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                        )
                        uiState.connectedSsid?.let { ssid ->
                            Text(
                                "Connected to: $ssid",
                                style = MaterialTheme.typography.labelMedium,
                                color = MaterialTheme.colorScheme.primary,
                            )
                        }
                    }
                }

                AddApplianceScanStatus.Found -> {
                    Column(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalAlignment = Alignment.CenterHorizontally,
                        verticalArrangement = Arrangement.spacedBy(12.dp),
                    ) {
                        CircularProgressIndicator()
                        Text("Appliance found — starting setup…", style = MaterialTheme.typography.bodyLarge)
                        uiState.detectedDeviceId?.let { id ->
                            Text("Device ID: $id", style = MaterialTheme.typography.bodyMedium)
                        }
                    }
                }

                else -> {
                    Text(
                        text = "Scan for a brand-new appliance on the setup Wi-Fi network, or add one already on your LAN.",
                        style = MaterialTheme.typography.bodyLarge,
                    )

                    uiState.connectedSsid?.takeIf {
                        ManagementApNetworkUtils.isSetupSsid(it)
                    }?.let { ssid ->
                        Text(
                            "Connected to $ssid",
                            style = MaterialTheme.typography.labelMedium,
                            color = MaterialTheme.colorScheme.primary,
                        )
                    }

                    uiState.errorMessage?.let {
                        Text(it, color = MaterialTheme.colorScheme.error)
                    }

                    Card(
                        modifier = Modifier.fillMaxWidth(),
                        colors = CardDefaults.cardColors(
                            containerColor = MaterialTheme.colorScheme.primaryContainer,
                        ),
                    ) {
                        Column(
                            modifier = Modifier.padding(20.dp),
                            verticalArrangement = Arrangement.spacedBy(12.dp),
                        ) {
                            Text("New appliance setup", style = MaterialTheme.typography.titleMedium)
                            Text(
                                "Power on your Renz-Fi, connect your phone to " +
                                    "\"${Constants.MANAGEMENT_AP_SSID}\" in Wi-Fi settings, then scan again.",
                                style = MaterialTheme.typography.bodyMedium,
                            )
                            Button(
                                onClick = onScanNearby,
                                enabled = !isScanning,
                                modifier = Modifier.fillMaxWidth(),
                            ) {
                                Icon(Icons.Default.Search, contentDescription = null)
                                Text("Scan nearby", modifier = Modifier.padding(start = 8.dp))
                            }
                            OutlinedButton(
                                onClick = {
                                    onOpenWifiSettings()
                                    context.startActivity(Intent(Settings.ACTION_WIFI_SETTINGS))
                                },
                                modifier = Modifier.fillMaxWidth(),
                            ) {
                                Icon(Icons.Default.Wifi, contentDescription = null)
                                Text("Open Wi-Fi settings", modifier = Modifier.padding(start = 8.dp))
                            }
                        }
                    }

                    Card(
                        modifier = Modifier.fillMaxWidth(),
                        onClick = onAddExisting,
                    ) {
                        Column(
                            modifier = Modifier.padding(20.dp),
                            verticalArrangement = Arrangement.spacedBy(8.dp),
                        ) {
                            Text("Add existing appliance", style = MaterialTheme.typography.titleMedium)
                            Text(
                                "Appliance already configured on your LAN — enter IP or discover it.",
                                style = MaterialTheme.typography.bodyMedium,
                            )
                        }
                    }
                }
            }

            if (uiState.scanStatus != AddApplianceScanStatus.Scanning &&
                uiState.scanStatus != AddApplianceScanStatus.Found
            ) {
                OutlinedButton(onClick = onBack, modifier = Modifier.fillMaxWidth()) {
                    Text("Cancel")
                }
            }
        }
    }
}
