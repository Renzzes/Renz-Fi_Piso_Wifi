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
import androidx.compose.material3.Button
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
import com.renzfi.owner.viewmodel.DetectedAppliance
import com.renzfi.owner.viewmodel.OnboardingPhase

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun OnboardingConnectScreen(
    phase: OnboardingPhase,
    detected: DetectedAppliance?,
    isLoading: Boolean,
    errorMessage: String?,
    onOpenWifiSettings: () -> Unit,
    onRetryDetect: () -> Unit,
    onContinue: () -> Unit,
    onCancel: () -> Unit,
    onBack: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val context = LocalContext.current
    val openWifi: () -> Unit = {
        onOpenWifiSettings()
        context.startActivity(Intent(Settings.ACTION_WIFI_SETTINGS))
    }

    Scaffold(
        modifier = modifier,
        topBar = {
            TopAppBar(
                title = {
                    Text(
                        when (phase) {
                            OnboardingPhase.Detected -> "Appliance found"
                            OnboardingPhase.Detecting -> "Detecting…"
                            else -> "Connect to appliance"
                        },
                    )
                },
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
            when (phase) {
                OnboardingPhase.Detecting -> {
                    Column(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalAlignment = Alignment.CenterHorizontally,
                        verticalArrangement = Arrangement.spacedBy(16.dp),
                    ) {
                        CircularProgressIndicator()
                        Text("Checking ${Constants.MANAGEMENT_AP_IP}…")
                    }
                }

                OnboardingPhase.Detected -> {
                    detected?.let { device ->
                        Text("Setup can continue on this appliance.", style = MaterialTheme.typography.bodyLarge)
                        InfoRow("Device ID", device.deviceId)
                        InfoRow("Firmware", device.firmwareVersion)
                        InfoRow("Hardware", device.hardwareRevision.ifBlank { "—" })
                        InfoRow("Build", device.buildLabel.ifBlank { "—" })
                        Button(onClick = onContinue, modifier = Modifier.fillMaxWidth()) {
                            Text("Continue setup")
                        }
                    }
                }

                else -> {
                    Text(
                        "Connect your phone to the appliance Management Wi-Fi:",
                        style = MaterialTheme.typography.bodyLarge,
                    )
                    Text(
                        text = Constants.MANAGEMENT_AP_SSID,
                        style = MaterialTheme.typography.titleMedium,
                        color = MaterialTheme.colorScheme.primary,
                    )
                    Text(
                        "Open Android Wi-Fi settings, join the open network, then return to this app.",
                        style = MaterialTheme.typography.bodyMedium,
                    )
                    errorMessage?.let {
                        Text(it, color = MaterialTheme.colorScheme.error)
                    }
                    Button(onClick = openWifi, modifier = Modifier.fillMaxWidth(), enabled = !isLoading) {
                        Text("Open Wi-Fi settings")
                    }
                    OutlinedButton(onClick = onRetryDetect, modifier = Modifier.fillMaxWidth()) {
                        Text("I've connected — detect appliance")
                    }
                }
            }

            if (phase != OnboardingPhase.Detecting) {
                OutlinedButton(onClick = onCancel, modifier = Modifier.fillMaxWidth()) {
                    Text("Cancel setup")
                }
            }
        }
    }
}

@Composable
private fun InfoRow(label: String, value: String) {
    Column(modifier = Modifier.fillMaxWidth()) {
        Text(label, style = MaterialTheme.typography.labelMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
        Text(value, style = MaterialTheme.typography.bodyLarge)
    }
}
