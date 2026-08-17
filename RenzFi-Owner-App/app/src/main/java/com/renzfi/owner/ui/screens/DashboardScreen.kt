package com.renzfi.owner.ui.screens

import android.content.Intent
import android.provider.Settings as AndroidProvider
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
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import com.renzfi.owner.ui.components.RenzFiTopBar

/**
 * Shown in the rare case that the NavGraph cannot resolve a valid device for
 * the admin_login route — e.g. if the back-stack is restored after the device
 * was deleted. Never crashes; always offers a back button.
 */
@Composable
fun DashboardFallbackScreen(
    onBack: () -> Unit,
    modifier: Modifier = Modifier,
    message: String = "Device configuration is incomplete.",
) {
    Scaffold(
        modifier = modifier,
        topBar = {
            RenzFiTopBar(
                title = "Admin Login",
                onBack = onBack,
                actions = {},
            )
        },
    ) { padding ->
        ApplianceUnavailableState(
            deviceName = "",
            headline = message,
            detail = "Return to My Vendo and verify the saved appliance details.",
            showRetry = false,
            showWifiSettings = false,
            modifier = Modifier
                .fillMaxSize()
                .padding(padding),
            onRetry = {},
            onOpenWifiSettings = {},
            onBackToDeviceManagement = onBack,
        )
    }
}

/**
 * Reusable offline / error state composable used by [DashboardFallbackScreen]
 * and [AdminLoginScreen]. Never exposes a blank page or a raw browser error.
 */
@Composable
fun ApplianceUnavailableState(
    deviceName: String,
    onRetry: () -> Unit,
    onOpenWifiSettings: () -> Unit,
    onBackToDeviceManagement: () -> Unit,
    modifier: Modifier = Modifier,
    headline: String = "Appliance unavailable",
    detail: String = "Connect to the same network as $deviceName and try again.",
    showRetry: Boolean = true,
    showWifiSettings: Boolean = true,
) {
    Column(
        modifier = modifier.padding(24.dp),
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
            text = headline,
            style = MaterialTheme.typography.headlineSmall,
            textAlign = TextAlign.Center,
            color = MaterialTheme.colorScheme.primary,
        )
        Spacer(modifier = Modifier.height(12.dp))
        Text(
            text = detail,
            style = MaterialTheme.typography.bodyLarge,
            textAlign = TextAlign.Center,
        )
        Spacer(modifier = Modifier.height(32.dp))
        if (showRetry) {
            Button(
                onClick = onRetry,
                modifier = Modifier.fillMaxWidth(),
            ) {
                Text("Retry")
            }
            Spacer(modifier = Modifier.height(12.dp))
        }
        if (showWifiSettings) {
            OutlinedButton(
                onClick = onOpenWifiSettings,
                modifier = Modifier.fillMaxWidth(),
            ) {
                Text("Open Wi-Fi Settings")
            }
            Spacer(modifier = Modifier.height(12.dp))
        }
        OutlinedButton(
            onClick = onBackToDeviceManagement,
            modifier = Modifier.fillMaxWidth(),
        ) {
            Text("Back to Device Management")
        }
    }
}

fun openWifiSettingsSafely(context: android.content.Context) {
    try {
        context.startActivity(
            Intent(AndroidProvider.ACTION_WIFI_SETTINGS).apply {
                addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
            },
        )
    } catch (_: Exception) {
        // No Wi-Fi settings activity — stay on screen.
    }
}
