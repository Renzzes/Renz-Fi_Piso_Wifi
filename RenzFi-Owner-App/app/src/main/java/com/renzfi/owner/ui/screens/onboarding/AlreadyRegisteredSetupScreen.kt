package com.renzfi.owner.ui.screens.onboarding

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.renzfi.owner.model.NearbyApplianceInfo
import com.renzfi.owner.model.VendoDevice

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun AlreadyRegisteredSetupScreen(
    device: VendoDevice,
    info: NearbyApplianceInfo,
    onOpenDashboard: () -> Unit,
    onMaintenanceMode: () -> Unit,
    onBack: () -> Unit,
    modifier: Modifier = Modifier,
) {
    Scaffold(
        modifier = modifier,
        topBar = {
            TopAppBar(title = { Text("Appliance registered") })
        },
    ) { padding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding)
                .padding(24.dp),
            verticalArrangement = Arrangement.spacedBy(16.dp),
        ) {
            Text(
                "This appliance is already registered",
                style = MaterialTheme.typography.headlineSmall,
                fontWeight = FontWeight.SemiBold,
            )
            Text(
                "You're connected to its setup Wi-Fi, but this device is already in your fleet.",
                style = MaterialTheme.typography.bodyLarge,
            )

            Card(modifier = Modifier.fillMaxWidth()) {
                Column(
                    modifier = Modifier.padding(20.dp),
                    verticalArrangement = Arrangement.spacedBy(8.dp),
                ) {
                    Text(device.name, style = MaterialTheme.typography.titleMedium)
                    Text("Device ID: ${info.deviceId}", style = MaterialTheme.typography.bodyMedium)
                    info.managementApMode?.let { mode ->
                        Text(
                            "Management AP: ${mode.replaceFirstChar { it.uppercase() }}",
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                        )
                    }
                }
            }

            Button(onClick = onOpenDashboard, modifier = Modifier.fillMaxWidth()) {
                Text("Open dashboard")
            }
            OutlinedButton(onClick = onMaintenanceMode, modifier = Modifier.fillMaxWidth()) {
                Text("Maintenance mode")
            }
            OutlinedButton(onClick = onBack, modifier = Modifier.fillMaxWidth()) {
                Text("Back to fleet")
            }
        }
    }
}
