package com.renzfi.owner.ui.screens

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.renzfi.owner.ui.components.OnlineStatusBadge
import com.renzfi.owner.ui.components.RenzFiTopBar
import com.renzfi.owner.util.DateUtils
import com.renzfi.owner.viewmodel.DeviceOverviewUiState

@Composable
fun DeviceOverviewScreen(
    uiState: DeviceOverviewUiState,
    onRefresh: () -> Unit,
    onBack: () -> Unit,
    modifier: Modifier = Modifier,
) {
    Scaffold(
        modifier = modifier,
        topBar = {
            RenzFiTopBar(title = "Device Overview", onBack = onBack)
        },
    ) { padding ->
        if (uiState.isLoading) {
            CircularProgressIndicator(
                modifier = Modifier
                    .fillMaxSize()
                    .padding(padding)
                    .padding(32.dp),
            )
        } else {
            val device = uiState.device
            if (device == null) {
                Text(
                    text = "Device not found.",
                    modifier = Modifier
                        .fillMaxSize()
                        .padding(padding)
                        .padding(24.dp),
                )
            } else {
                Column(
                    modifier = Modifier
                        .fillMaxSize()
                        .padding(padding)
                        .padding(24.dp),
                ) {
                    Text(
                        text = "Status information only. Sales, vouchers, and monitoring summaries coming soon.",
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.7f),
                    )
                    Spacer(modifier = Modifier.height(24.dp))
                    OverviewCard(label = "Name", value = device.name)
                    Spacer(modifier = Modifier.height(12.dp))
                    OverviewCard(
                        label = "MikroTik DDNS",
                        value = device.mikrotikDdns.ifBlank { "Not configured" },
                    )
                    Spacer(modifier = Modifier.height(12.dp))
                    OverviewCard(label = "ESP32 IP", value = device.esp32LocalIp)
                    Spacer(modifier = Modifier.height(12.dp))
                    Card(
                        modifier = Modifier.fillMaxWidth(),
                        colors = CardDefaults.cardColors(
                            containerColor = MaterialTheme.colorScheme.surfaceVariant,
                        ),
                    ) {
                        Column(modifier = Modifier.padding(16.dp)) {
                            Text(
                                text = "Status",
                                style = MaterialTheme.typography.labelLarge,
                                color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.7f),
                            )
                            Spacer(modifier = Modifier.height(4.dp))
                            OnlineStatusBadge(device = device)
                        }
                    }
                    Spacer(modifier = Modifier.height(12.dp))
                    OverviewCard(
                        label = "Last Seen",
                        value = DateUtils.formatTimestamp(device.lastSeen),
                    )
                    Spacer(modifier = Modifier.height(24.dp))
                    Button(
                        onClick = onRefresh,
                        modifier = Modifier.fillMaxWidth(),
                    ) {
                        Text("Refresh Status")
                    }
                }
            }
        }
    }
}

@Composable
private fun OverviewCard(
    label: String,
    value: String,
    modifier: Modifier = Modifier,
) {
    Card(
        modifier = modifier.fillMaxWidth(),
        colors = CardDefaults.cardColors(
            containerColor = MaterialTheme.colorScheme.surfaceVariant,
        ),
    ) {
        Column(modifier = Modifier.padding(16.dp)) {
            Text(
                text = label,
                style = MaterialTheme.typography.labelLarge,
                color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.7f),
            )
            Spacer(modifier = Modifier.height(4.dp))
            Text(
                text = value,
                style = MaterialTheme.typography.bodyLarge,
                fontWeight = FontWeight.Medium,
            )
        }
    }
}
