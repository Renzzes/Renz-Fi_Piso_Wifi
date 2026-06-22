package com.renzfi.owner.ui.components

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.size
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.renzfi.owner.model.DeviceOnlineStatus
import com.renzfi.owner.model.VendoDevice
import com.renzfi.owner.model.onlineStatus

@Composable
fun OnlineStatusBadge(
    device: VendoDevice,
    modifier: Modifier = Modifier,
) {
    val status = device.onlineStatus()
    val (emoji, label, color) = when (status) {
        DeviceOnlineStatus.Online -> Triple("🟢", "Online", MaterialTheme.colorScheme.primary)
        DeviceOnlineStatus.Offline -> Triple("🔴", "Offline", MaterialTheme.colorScheme.error)
        DeviceOnlineStatus.Unknown -> Triple("⚪", "Unknown", MaterialTheme.colorScheme.onSurface.copy(alpha = 0.6f))
    }

    Row(
        modifier = modifier,
        horizontalArrangement = Arrangement.spacedBy(6.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(text = emoji, style = MaterialTheme.typography.bodyMedium)
        Text(
            text = label,
            style = MaterialTheme.typography.bodyMedium,
            color = color,
        )
    }
}
