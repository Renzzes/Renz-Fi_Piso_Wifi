package com.renzfi.owner.ui.screens

import androidx.compose.foundation.Image
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SegmentedButton
import androidx.compose.material3.SegmentedButtonDefaults
import androidx.compose.material3.SingleChoiceSegmentedButtonRow
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import com.renzfi.owner.BuildConfig
import com.renzfi.owner.R
import com.renzfi.owner.ui.components.RenzFiTopBar
import com.renzfi.owner.ui.components.UpdateAvailableDialog
import com.renzfi.owner.update.UpdateState
import com.renzfi.owner.util.Constants
import com.renzfi.owner.util.DateUtils
import com.renzfi.owner.viewmodel.AboutUiState
import java.io.File

@Composable
fun AboutScreen(
    uiState: AboutUiState,
    onBack: () -> Unit,
    onCheckForUpdates: () -> Unit,
    onUpdateNow: () -> Unit,
    onDismissUpdate: () -> Unit,
    onCancelDownload: () -> Unit,
    onChannelChange: (String) -> Unit,
    onInstallUpdate: () -> Unit,
    onResetError: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val snackbarHostState = remember { SnackbarHostState() }

    // Show error in snackbar and reset state
    val errorMessage = (uiState.updateState as? UpdateState.Error)?.message
    LaunchedEffect(errorMessage) {
        if (!errorMessage.isNullOrBlank()) {
            snackbarHostState.showSnackbar(errorMessage)
            onResetError()
        }
    }

    // Update available dialog — suppressed if user dismissed this version
    if (uiState.showUpdateDialog) {
        uiState.pendingManifest?.let { manifest ->
            UpdateAvailableDialog(
                manifest = manifest,
                installedVersion = uiState.installedVersion,
                onUpdateNow = onUpdateNow,
                onDismiss = onDismissUpdate,
            )
        }
    }

    Scaffold(
        modifier = modifier,
        topBar = {
            RenzFiTopBar(
                title = stringResource(R.string.about_title),
                onBack = onBack,
            )
        },
        snackbarHost = { SnackbarHost(snackbarHostState) },
    ) { padding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding)
                .verticalScroll(rememberScrollState())
                .padding(24.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            // ── App identity ─────────────────────────────────────────────────
            Image(
                painter = painterResource(id = R.drawable.renzfi_logo),
                contentDescription = stringResource(R.string.logo_content_description),
                modifier = Modifier.size(80.dp),
            )
            Spacer(modifier = Modifier.height(16.dp))
            Text(
                text = stringResource(R.string.about_app_name),
                style = MaterialTheme.typography.headlineMedium,
                fontWeight = FontWeight.Bold,
                color = MaterialTheme.colorScheme.primary,
            )
            Spacer(modifier = Modifier.height(4.dp))
            Text(
                text = "Version ${BuildConfig.VERSION_NAME}  ·  Build ${BuildConfig.VERSION_CODE}",
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.65f),
            )
            Spacer(modifier = Modifier.height(8.dp))
            Text(
                text = stringResource(R.string.about_description),
                style = MaterialTheme.typography.bodySmall,
                textAlign = TextAlign.Center,
                color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.75f),
            )

            Spacer(modifier = Modifier.height(24.dp))
            HorizontalDivider()
            Spacer(modifier = Modifier.height(20.dp))

            // ── Update hub ───────────────────────────────────────────────────
            UpdateHubCard(
                uiState = uiState,
                onChannelChange = onChannelChange,
                onCheckForUpdates = onCheckForUpdates,
                onCancelDownload = onCancelDownload,
                onInstallUpdate = onInstallUpdate,
            )

            Spacer(modifier = Modifier.height(20.dp))
            HorizontalDivider()
            Spacer(modifier = Modifier.height(16.dp))

            // ── Footer note ──────────────────────────────────────────────────
            Text(
                text = stringResource(R.string.about_vpn_note),
                style = MaterialTheme.typography.bodySmall,
                textAlign = TextAlign.Center,
                color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.5f),
            )
        }
    }
}

@Composable
private fun UpdateHubCard(
    uiState: AboutUiState,
    onChannelChange: (String) -> Unit,
    onCheckForUpdates: () -> Unit,
    onCancelDownload: () -> Unit,
    onInstallUpdate: () -> Unit,
) {
    Card(
        modifier = Modifier.fillMaxWidth(),
        colors = CardDefaults.cardColors(
            containerColor = MaterialTheme.colorScheme.surfaceVariant,
        ),
    ) {
        Column(
            modifier = Modifier.padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            Text(
                text = "App Updates",
                style = MaterialTheme.typography.titleMedium,
                fontWeight = FontWeight.SemiBold,
            )

            // ── Channel toggle ────────────────────────────────────────────────
            val channels = listOf(
                Constants.UPDATE_CHANNEL_STABLE to "Stable",
                Constants.UPDATE_CHANNEL_BETA to "Beta",
            )
            SingleChoiceSegmentedButtonRow(modifier = Modifier.fillMaxWidth()) {
                channels.forEachIndexed { index, (value, label) ->
                    SegmentedButton(
                        selected = uiState.updateChannel == value,
                        onClick = { onChannelChange(value) },
                        shape = SegmentedButtonDefaults.itemShape(
                            index = index,
                            count = channels.size,
                        ),
                    ) {
                        Text(label)
                    }
                }
            }

            // ── Last checked ─────────────────────────────────────────────────
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
            ) {
                Text(
                    text = "Last checked",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.65f),
                )
                Text(
                    text = if (uiState.lastUpdateCheckAt == 0L) "Never"
                    else DateUtils.formatTimestamp(uiState.lastUpdateCheckAt),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.65f),
                )
            }

            HorizontalDivider()

            // ── Status + action area ──────────────────────────────────────────
            when (val state = uiState.updateState) {
                is UpdateState.Idle, is UpdateState.UpToDate -> {
                    if (state is UpdateState.UpToDate) {
                        Text(
                            text = "Renz-Fi Manager is up to date.",
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.primary,
                        )
                    }
                    Button(
                        onClick = onCheckForUpdates,
                        modifier = Modifier.fillMaxWidth(),
                    ) {
                        Text("Check for Updates")
                    }
                }

                is UpdateState.Checking -> {
                    Row(
                        horizontalArrangement = Arrangement.spacedBy(8.dp),
                        verticalAlignment = Alignment.CenterVertically,
                    ) {
                        CircularProgressIndicator(modifier = Modifier.size(16.dp), strokeWidth = 2.dp)
                        Text(
                            text = "Checking for updates…",
                            style = MaterialTheme.typography.bodySmall,
                        )
                    }
                }

                is UpdateState.Available -> {
                    Text(
                        text = "Update available: ${state.manifest.version}",
                        style = MaterialTheme.typography.bodySmall,
                        fontWeight = FontWeight.SemiBold,
                        color = MaterialTheme.colorScheme.primary,
                    )
                    Button(
                        onClick = { /* dialog handles this via showUpdateDialog */ },
                        modifier = Modifier.fillMaxWidth(),
                        enabled = false,
                    ) {
                        Text("Update Now")
                    }
                }

                is UpdateState.Downloading -> {
                    Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
                        val progressLabel = if (state.progressPercent >= 0)
                            "Downloading… ${state.progressPercent}%"
                        else
                            "Downloading…"
                        Text(
                            text = progressLabel,
                            style = MaterialTheme.typography.bodySmall,
                        )
                        if (state.progressPercent >= 0) {
                            LinearProgressIndicator(
                                progress = { state.progressPercent / 100f },
                                modifier = Modifier.fillMaxWidth(),
                            )
                        } else {
                            LinearProgressIndicator(modifier = Modifier.fillMaxWidth())
                        }
                        TextButton(
                            onClick = onCancelDownload,
                            modifier = Modifier.align(Alignment.End),
                            colors = ButtonDefaults.textButtonColors(
                                contentColor = MaterialTheme.colorScheme.error,
                            ),
                        ) {
                            Text("Cancel")
                        }
                    }
                }

                is UpdateState.Verifying -> {
                    Row(
                        horizontalArrangement = Arrangement.spacedBy(8.dp),
                        verticalAlignment = Alignment.CenterVertically,
                    ) {
                        CircularProgressIndicator(modifier = Modifier.size(16.dp), strokeWidth = 2.dp)
                        Text(
                            text = "Verifying SHA-256…",
                            style = MaterialTheme.typography.bodySmall,
                        )
                    }
                }

                is UpdateState.ReadyToInstall -> {
                    Text(
                        text = "Download verified. Ready to install.",
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.primary,
                        fontWeight = FontWeight.SemiBold,
                    )
                    Button(
                        onClick = onInstallUpdate,
                        modifier = Modifier.fillMaxWidth(),
                    ) {
                        Text("Install Update")
                    }
                }

                is UpdateState.Installing -> {
                    Text(
                        text = "Launching installer…",
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.65f),
                    )
                }

                is UpdateState.Error -> {
                    // Error is shown via snackbar; just restore the check button
                    OutlinedButton(
                        onClick = onCheckForUpdates,
                        modifier = Modifier.fillMaxWidth(),
                    ) {
                        Text("Retry")
                    }
                }
            }
        }
    }
}
