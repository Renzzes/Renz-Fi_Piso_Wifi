package com.renzfi.owner.ui.screens

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material.icons.filled.WifiOff
import androidx.compose.material3.Button
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import com.renzfi.owner.R
import com.renzfi.owner.model.VendoDevice

@Composable
fun ConnectionHelpScreen(
    device: VendoDevice?,
    errorMessage: String?,
    onRetry: () -> Unit,
    onOpenSettings: () -> Unit,
    onBackToDevices: (() -> Unit)? = null,
    modifier: Modifier = Modifier,
) {
    Column(
        modifier = modifier
            .fillMaxSize()
            .padding(24.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center,
    ) {
        Icon(
            imageVector = Icons.Default.WifiOff,
            contentDescription = null,
            modifier = Modifier.height(64.dp),
            tint = MaterialTheme.colorScheme.error,
        )
        Spacer(modifier = Modifier.height(24.dp))
        Text(
            text = stringResource(R.string.connection_help_title),
            style = MaterialTheme.typography.headlineLarge,
            textAlign = TextAlign.Center,
            color = MaterialTheme.colorScheme.primary,
        )
        if (device != null) {
            Spacer(modifier = Modifier.height(8.dp))
            Text(
                text = device.name,
                style = MaterialTheme.typography.titleMedium,
                textAlign = TextAlign.Center,
            )
            Text(
                text = device.esp32LocalIp,
                style = MaterialTheme.typography.bodyMedium,
                textAlign = TextAlign.Center,
                color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.7f),
            )
        }
        Spacer(modifier = Modifier.height(16.dp))
        Text(
            text = "Unable to communicate with the appliance.",
            style = MaterialTheme.typography.bodyLarge,
            textAlign = TextAlign.Center,
        )
        if (!errorMessage.isNullOrBlank()) {
            Spacer(modifier = Modifier.height(8.dp))
            Text(
                text = errorMessage,
                style = MaterialTheme.typography.bodyMedium,
                textAlign = TextAlign.Center,
                color = MaterialTheme.colorScheme.error,
            )
        }
        Spacer(modifier = Modifier.height(24.dp))
        Text(
            text = "Please:\n\n" +
                "1. Connect to the Renz-Fi network\n" +
                "2. Verify the appliance is powered on\n" +
                "3. Confirm the ESP32 IP in Settings\n" +
                "4. Tap Retry",
            style = MaterialTheme.typography.bodyLarge,
            textAlign = TextAlign.Start,
            modifier = Modifier.fillMaxWidth(),
        )
        Spacer(modifier = Modifier.height(32.dp))
        Button(
            onClick = onRetry,
            modifier = Modifier.fillMaxWidth(),
        ) {
            Text("Retry")
        }
        Spacer(modifier = Modifier.height(12.dp))
        OutlinedButton(
            onClick = onOpenSettings,
            modifier = Modifier.fillMaxWidth(),
        ) {
            Icon(
                imageVector = Icons.Default.Settings,
                contentDescription = null,
            )
            Text(
                text = "Settings",
                modifier = Modifier.padding(start = 8.dp),
            )
        }
        if (onBackToDevices != null) {
            Spacer(modifier = Modifier.height(12.dp))
            OutlinedButton(
                onClick = onBackToDevices,
                modifier = Modifier.fillMaxWidth(),
            ) {
                Text("Back to Devices")
            }
        }
    }
}
