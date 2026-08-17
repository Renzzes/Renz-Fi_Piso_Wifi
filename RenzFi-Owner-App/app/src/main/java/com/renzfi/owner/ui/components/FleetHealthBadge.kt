package com.renzfi.owner.ui.components

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Row
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.renzfi.owner.model.FleetApplianceHealth
import com.renzfi.owner.model.FleetHealthCalculator
import com.renzfi.owner.model.FleetHealthLevel
import com.renzfi.owner.util.DateUtils

@Composable
fun FleetHealthBadge(
    health: FleetApplianceHealth?,
    lastSeenMs: Long?,
    modifier: Modifier = Modifier,
) {
    val level = health?.level ?: FleetHealthLevel.Offline
    val emoji = FleetHealthCalculator.emoji(level)
    val label = FleetHealthCalculator.label(level)
    val score = health?.score ?: 0
    val color = when (level) {
        FleetHealthLevel.Healthy -> MaterialTheme.colorScheme.primary
        FleetHealthLevel.Warning -> MaterialTheme.colorScheme.tertiary
        FleetHealthLevel.Offline -> MaterialTheme.colorScheme.error
    }

    Row(
        modifier = modifier,
        horizontalArrangement = Arrangement.spacedBy(6.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(text = emoji, style = MaterialTheme.typography.bodyMedium)
        Text(
            text = "$label · Score $score",
            style = MaterialTheme.typography.bodyMedium,
            color = color,
        )
        lastSeenMs?.let {
            Text(
                text = "· ${DateUtils.formatRelativeTime(it)}",
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.6f),
            )
        }
    }
}
