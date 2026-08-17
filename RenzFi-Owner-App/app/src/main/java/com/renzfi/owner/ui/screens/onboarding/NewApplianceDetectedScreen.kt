package com.renzfi.owner.ui.screens.onboarding

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.CheckCircle
import androidx.compose.material.icons.filled.Error
import androidx.compose.material.icons.filled.Info
import androidx.compose.material.icons.filled.Warning
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.renzfi.owner.model.ApplianceBuildDetails
import com.renzfi.owner.model.ApplianceReadinessSummary
import com.renzfi.owner.model.GatewayProductStatus
import com.renzfi.owner.model.NearbyApplianceInfo
import com.renzfi.owner.model.ReadinessCheck
import com.renzfi.owner.model.ReadinessTone

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun NewApplianceDetectedScreen(
    info: NearbyApplianceInfo,
    onBeginSetup: () -> Unit,
    onLater: () -> Unit,
    modifier: Modifier = Modifier,
) {
    Scaffold(
        modifier = modifier,
        topBar = {
            TopAppBar(title = { Text("Appliance readiness") })
        },
    ) { padding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding)
                .padding(24.dp),
            verticalArrangement = Arrangement.spacedBy(16.dp),
        ) {
            ApplianceIdentityCard(
                deviceId = info.deviceId,
                firmwareVersion = info.firmwareVersion,
                buildDetails = info.buildDetails,
            )

            ReadinessSection(
                title = "Hardware status",
                checks = info.readiness.hardwareChecks,
            )

            GatewayStatusSection(readiness = info.readiness)

            Text(
                info.readiness.headline,
                style = MaterialTheme.typography.titleMedium,
                fontWeight = FontWeight.SemiBold,
                color = if (info.readiness.readyForInstallation) {
                    MaterialTheme.colorScheme.primary
                } else {
                    MaterialTheme.colorScheme.error
                },
            )

            Button(
                onClick = onBeginSetup,
                enabled = info.readiness.readyForInstallation,
                modifier = Modifier.fillMaxWidth(),
            ) {
                Text("Begin setup")
            }
            OutlinedButton(onClick = onLater, modifier = Modifier.fillMaxWidth()) {
                Text("Later")
            }
        }
    }
}

@Composable
private fun ApplianceIdentityCard(
    deviceId: String,
    firmwareVersion: String,
    buildDetails: ApplianceBuildDetails,
) {
    Card(
        modifier = Modifier.fillMaxWidth(),
        colors = CardDefaults.cardColors(
            containerColor = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.45f),
        ),
    ) {
        Column(
            modifier = Modifier.padding(20.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            Text(
                "Renz-Fi appliance",
                style = MaterialTheme.typography.titleMedium,
                fontWeight = FontWeight.SemiBold,
            )
            Text(
                deviceId,
                style = MaterialTheme.typography.headlineSmall,
                fontWeight = FontWeight.Bold,
            )

            HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant)

            IdentityLine(label = "Firmware", value = firmwareVersion)
            buildDetails.adminBuild?.let { IdentityLine(label = "Admin build", value = it) }
            buildDetails.buildNumber?.let { IdentityLine(label = "Build #", value = it.toString()) }
            buildDetails.gitCommit?.let { commit ->
                IdentityLine(label = "Git", value = commit.take(7))
            }
        }
    }
}

@Composable
private fun GatewayStatusSection(readiness: ApplianceReadinessSummary) {
    Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
        Text(
            "Gateway status",
            style = MaterialTheme.typography.titleSmall,
            fontWeight = FontWeight.SemiBold,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
        Card(modifier = Modifier.fillMaxWidth()) {
            Column(modifier = Modifier.padding(vertical = 8.dp)) {
                GatewayProductRow(product = readiness.gatewayProduct)
                if (readiness.gatewayCapabilities.isNotEmpty()) {
                    HorizontalDivider(modifier = Modifier.padding(horizontal = 20.dp))
                    readiness.gatewayCapabilities.forEachIndexed { index, check ->
                        ReadinessRow(check = check)
                        if (index < readiness.gatewayCapabilities.lastIndex) {
                            HorizontalDivider(modifier = Modifier.padding(horizontal = 20.dp))
                        }
                    }
                }
                if (readiness.gatewayChecks.isNotEmpty()) {
                    HorizontalDivider(modifier = Modifier.padding(horizontal = 20.dp))
                    readiness.gatewayChecks.forEachIndexed { index, check ->
                        ReadinessRow(check = check)
                        if (index < readiness.gatewayChecks.lastIndex) {
                            HorizontalDivider(modifier = Modifier.padding(horizontal = 20.dp))
                        }
                    }
                }
            }
        }
    }
}

@Composable
private fun GatewayProductRow(product: GatewayProductStatus) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 20.dp, vertical = 14.dp),
        horizontalArrangement = Arrangement.spacedBy(12.dp),
        verticalAlignment = Alignment.Top,
    ) {
        Icon(
            imageVector = product.tone.icon(),
            contentDescription = null,
            modifier = Modifier.size(22.dp),
            tint = product.tone.color(),
        )
        Column(modifier = Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(2.dp)) {
            Text(
                "Gateway",
                style = MaterialTheme.typography.labelMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            Text(
                product.name,
                style = MaterialTheme.typography.bodyLarge,
                fontWeight = FontWeight.SemiBold,
            )
            product.subtitle?.let { subtitle ->
                Text(
                    subtitle,
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
        }
        Text(
            product.connectionLabel,
            style = MaterialTheme.typography.bodyMedium,
            fontWeight = FontWeight.Medium,
            color = product.tone.color(),
        )
    }
}

@Composable
private fun ReadinessSection(
    title: String,
    checks: List<ReadinessCheck>,
) {
    Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
        Text(
            title,
            style = MaterialTheme.typography.titleSmall,
            fontWeight = FontWeight.SemiBold,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
        Card(modifier = Modifier.fillMaxWidth()) {
            Column(modifier = Modifier.padding(vertical = 8.dp)) {
                checks.forEachIndexed { index, check ->
                    ReadinessRow(check = check)
                    if (index < checks.lastIndex) {
                        HorizontalDivider(modifier = Modifier.padding(horizontal = 20.dp))
                    }
                }
            }
        }
    }
}

@Composable
private fun IdentityLine(label: String, value: String) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.SpaceBetween,
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(
            label,
            style = MaterialTheme.typography.labelLarge,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
        Text(
            value,
            style = MaterialTheme.typography.bodyLarge,
            fontWeight = FontWeight.Medium,
        )
    }
}

@Composable
private fun ReadinessRow(check: ReadinessCheck) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 20.dp, vertical = 14.dp),
        horizontalArrangement = Arrangement.spacedBy(12.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Icon(
            imageVector = check.tone.icon(),
            contentDescription = null,
            modifier = Modifier.size(22.dp),
            tint = check.tone.color(),
        )
        when {
            check.tone == ReadinessTone.Expected && !check.detail.isNullOrBlank() -> {
                Text(
                    "${check.label} ${check.detail.replaceFirstChar { it.lowercase() }}",
                    modifier = Modifier.weight(1f),
                    style = MaterialTheme.typography.bodyLarge,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
            check.detail.isNullOrBlank() || check.tone == ReadinessTone.Ready -> {
                Text(
                    check.label,
                    modifier = Modifier.weight(1f),
                    style = MaterialTheme.typography.bodyLarge,
                )
            }
            else -> {
                Text(
                    check.label,
                    modifier = Modifier.weight(1f),
                    style = MaterialTheme.typography.bodyLarge,
                )
                Text(
                    check.detail,
                    style = MaterialTheme.typography.bodyMedium,
                    fontWeight = FontWeight.Medium,
                    color = check.tone.color(),
                )
            }
        }
    }
}

@Composable
private fun ReadinessTone.icon(): ImageVector = when (this) {
    ReadinessTone.Ready -> Icons.Default.CheckCircle
    ReadinessTone.Expected -> Icons.Default.Info
    ReadinessTone.Warning -> Icons.Default.Warning
    ReadinessTone.Issue -> Icons.Default.Error
}

@Composable
private fun ReadinessTone.color() = when (this) {
    ReadinessTone.Ready -> MaterialTheme.colorScheme.primary
    ReadinessTone.Expected -> MaterialTheme.colorScheme.onSurfaceVariant
    ReadinessTone.Warning -> MaterialTheme.colorScheme.tertiary
    ReadinessTone.Issue -> MaterialTheme.colorScheme.error
}
