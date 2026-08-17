package com.renzfi.owner.ui.screens.onboarding

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.selection.selectable
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.RadioButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.unit.dp
import com.renzfi.owner.model.NetworkMode
import com.renzfi.owner.model.NetworkStatusData
import com.renzfi.owner.model.ProvisioningCheck
import com.renzfi.owner.model.WorkflowResponse
import com.renzfi.owner.model.resolvedEthernet
import com.renzfi.owner.model.resolvedManagementAp
import com.renzfi.owner.util.ProductBranding
import com.renzfi.owner.viewmodel.OnboardingDraft
import com.renzfi.owner.viewmodel.WizardStep

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun OnboardingWizardScreen(
    wizardStep: WizardStep,
    workflow: WorkflowResponse?,
    draft: OnboardingDraft,
    networkStatus: NetworkStatusData?,
    validationChecks: List<ProvisioningCheck>,
    validationPassed: Boolean,
    isLoading: Boolean,
    errorMessage: String?,
    onBack: () -> Unit,
    onCancel: () -> Unit,
    onWelcomeContinue: () -> Unit,
    onNetworkModeChange: (NetworkMode) -> Unit,
    onNetworkTypeSubmit: () -> Unit,
    onRouterHostChange: (String) -> Unit,
    onRouterUsernameChange: (String) -> Unit,
    onRouterPasswordChange: (String) -> Unit,
    onRouterSubmit: () -> Unit,
    onPortalSubmit: () -> Unit,
    onCoinSubmit: () -> Unit,
    onCoinSkip: () -> Unit,
    onValidate: () -> Unit,
    onSummaryContinue: () -> Unit,
    onKeepManagementApChange: (Boolean) -> Unit,
    onFinish: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val progress = workflow?.installation?.progressPercent ?: 0

    Scaffold(
        modifier = modifier,
        topBar = {
            TopAppBar(
                title = { Text(stepTitle(wizardStep)) },
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
                .verticalScroll(rememberScrollState())
                .padding(24.dp),
            verticalArrangement = Arrangement.spacedBy(16.dp),
        ) {
            LinearProgressIndicator(
                progress = { progress / 100f },
                modifier = Modifier.fillMaxWidth(),
            )
            Text("$progress% complete", style = MaterialTheme.typography.labelMedium)

            networkStatus?.let { NetworkStatusPanel(it) }

            if (isLoading) {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.Center,
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    CircularProgressIndicator(modifier = Modifier.padding(end = 12.dp))
                    Text("Working…")
                }
            }

            errorMessage?.let {
                Text(it, color = MaterialTheme.colorScheme.error)
            }

            when (wizardStep) {
                WizardStep.Welcome -> WelcomeStep(onWelcomeContinue, isLoading)
                WizardStep.NetworkType -> NetworkTypeStep(
                    draft = draft,
                    onNetworkModeChange = onNetworkModeChange,
                    onSubmit = onNetworkTypeSubmit,
                    isLoading = isLoading,
                )
                WizardStep.RouterConnection -> RouterConnectionStep(
                    draft = draft,
                    onHostChange = onRouterHostChange,
                    onUsernameChange = onRouterUsernameChange,
                    onPasswordChange = onRouterPasswordChange,
                    onSubmit = onRouterSubmit,
                    isLoading = isLoading,
                )
                WizardStep.PortalConfiguration -> PortalStep(onSubmit = onPortalSubmit, isLoading = isLoading)
                WizardStep.CoinConfiguration -> CoinStep(
                    onSubmit = onCoinSubmit,
                    onSkip = onCoinSkip,
                    isLoading = isLoading,
                )
                WizardStep.Validation -> ValidationStep(
                    checks = validationChecks,
                    passed = validationPassed,
                    onValidate = onValidate,
                    isLoading = isLoading,
                )
                WizardStep.Summary -> SummaryStep(
                    workflow = workflow,
                    onContinue = onSummaryContinue,
                )
                WizardStep.Finish -> FinishStep(
                    draft = draft,
                    onKeepManagementApChange = onKeepManagementApChange,
                    onFinish = onFinish,
                    isLoading = isLoading,
                )
            }

            OutlinedButton(onClick = onCancel, modifier = Modifier.fillMaxWidth()) {
                Text("Cancel setup")
            }
        }
    }
}

@Composable
private fun NetworkStatusPanel(status: NetworkStatusData) {
    val mgmt = status.resolvedManagementAp()
    val eth = status.resolvedEthernet()
    Card(modifier = Modifier.fillMaxWidth()) {
        Column(modifier = Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Text("Network", style = MaterialTheme.typography.titleSmall)
            Text(status.modeLabel ?: status.mode.orEmpty(), style = MaterialTheme.typography.bodyMedium)
            mgmt?.let {
                Text("Management AP: ${it.ssid ?: "—"} · ${it.ip ?: "—"} · ${it.clients ?: 0} clients")
            }
            eth?.let {
                Text("Ethernet: ${it.ip ?: "—"} · gateway ${it.gateway ?: "—"}")
                Text("Link: ${if (it.link || it.linkUp) "up" else "down"}")
            }
        }
    }
}

@Composable
private fun WelcomeStep(onContinue: () -> Unit, isLoading: Boolean) {
    Text(
        "This wizard configures your appliance using the same provisioning APIs as the browser setup.",
        style = MaterialTheme.typography.bodyLarge,
    )
    Button(onClick = onContinue, enabled = !isLoading, modifier = Modifier.fillMaxWidth()) {
        Text("Start or resume setup")
    }
}

@Composable
private fun NetworkTypeStep(
    draft: OnboardingDraft,
    onNetworkModeChange: (NetworkMode) -> Unit,
    onSubmit: () -> Unit,
    isLoading: Boolean,
) {
    // Phase 7D: Renz-Fi Gateway is the official required router.
    // No network type choice is presented — setup proceeds directly to RouterOS.
    if (draft.mikrotikDetected) {
        Text(
            "${ProductBranding.GATEWAY_NAME} detected.",
            color = MaterialTheme.colorScheme.primary,
            style = MaterialTheme.typography.bodyMedium,
        )
    }

    Card(modifier = Modifier.fillMaxWidth()) {
        Column(modifier = Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                Text(ProductBranding.GATEWAY_NAME, style = MaterialTheme.typography.titleSmall)
                Text(
                    ProductBranding.GATEWAY_BADGE,
                    style = MaterialTheme.typography.labelSmall,
                    color = MaterialTheme.colorScheme.primary,
                )
            }
            Text(
                ProductBranding.GATEWAY_SUBTITLE,
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            Text(
                ProductBranding.GATEWAY_DESCRIPTION,
                style = MaterialTheme.typography.bodySmall,
            )
        }
    }

    Card(modifier = Modifier.fillMaxWidth()) {
        Column(modifier = Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Text("Optional: Wi-Fi access points", style = MaterialTheme.typography.titleSmall)
            Text(
                ProductBranding.AP_REQUIREMENT,
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            Text("Examples:", style = MaterialTheme.typography.labelMedium)
            ProductBranding.AP_EXAMPLES.forEach { example ->
                Text("• $example", style = MaterialTheme.typography.bodySmall)
            }
        }
    }

    Button(onClick = onSubmit, enabled = !isLoading, modifier = Modifier.fillMaxWidth()) {
        Text("Continue to gateway setup")
    }
}

@Composable
private fun RouterConnectionStep(
    draft: OnboardingDraft,
    onHostChange: (String) -> Unit,
    onUsernameChange: (String) -> Unit,
    onPasswordChange: (String) -> Unit,
    onSubmit: () -> Unit,
    isLoading: Boolean,
) {
    Text(
        ProductBranding.GATEWAY_CONNECT_PROMPT,
        style = MaterialTheme.typography.bodyLarge,
    )
    OutlinedTextField(
        value = draft.routerHost,
        onValueChange = onHostChange,
        label = { Text("Gateway IP") },
        placeholder = { Text("10.40.0.1") },
        modifier = Modifier.fillMaxWidth(),
        singleLine = true,
    )
    OutlinedTextField(
        value = draft.routerUsername,
        onValueChange = onUsernameChange,
        label = { Text("Username") },
        modifier = Modifier.fillMaxWidth(),
        singleLine = true,
    )
    OutlinedTextField(
        value = draft.routerPassword,
        onValueChange = onPasswordChange,
        label = { Text("Password") },
        modifier = Modifier.fillMaxWidth(),
        singleLine = true,
    )
    Button(onClick = onSubmit, enabled = !isLoading, modifier = Modifier.fillMaxWidth()) {
        Text("Connect gateway")
    }
}

@Composable
private fun PortalStep(onSubmit: () -> Unit, isLoading: Boolean) {
    Text(
        "Apply recommended captive portal defaults. Customize promos and branding later in Admin.",
        style = MaterialTheme.typography.bodyLarge,
    )
    Button(onClick = onSubmit, enabled = !isLoading, modifier = Modifier.fillMaxWidth()) {
        Text("Configure portal")
    }
}

@Composable
private fun CoinStep(onSubmit: () -> Unit, onSkip: () -> Unit, isLoading: Boolean) {
    Text(
        "Apply recommended coin slot defaults, or skip if coin hardware is not installed yet.",
        style = MaterialTheme.typography.bodyLarge,
    )
    Button(onClick = onSubmit, enabled = !isLoading, modifier = Modifier.fillMaxWidth()) {
        Text("Configure coin slot")
    }
    OutlinedButton(onClick = onSkip, enabled = !isLoading, modifier = Modifier.fillMaxWidth()) {
        Text("Skip for now")
    }
}

@Composable
private fun ValidationStep(
    checks: List<ProvisioningCheck>,
    passed: Boolean,
    onValidate: () -> Unit,
    isLoading: Boolean,
) {
    Text("Run installation checks on the appliance.", style = MaterialTheme.typography.bodyLarge)
    if (checks.isNotEmpty()) {
        checks.forEach { check ->
            val status = if (check.passed) "✓" else "✗"
            Text("$status ${check.id}${check.detail?.let { ": $it" } ?: ""}")
        }
    }
    Button(onClick = onValidate, enabled = !isLoading, modifier = Modifier.fillMaxWidth()) {
        Text(if (checks.isEmpty()) "Run checks" else "Re-run checks")
    }
    if (passed) {
        Text("All checks passed.", color = MaterialTheme.colorScheme.primary)
    }
}

@Composable
private fun SummaryStep(workflow: WorkflowResponse?, onContinue: () -> Unit) {
    val installation = workflow?.installation
    Text("Review installation progress before finishing.", style = MaterialTheme.typography.bodyLarge)
    Text("State: ${installation?.state ?: "—"}")
    Text("Progress: ${installation?.progressPercent ?: 0}%")
    installation?.completedSteps?.takeIf { it.isNotEmpty() }?.let { steps ->
        Text("Completed: ${steps.joinToString(", ")}")
    }
    Button(onClick = onContinue, modifier = Modifier.fillMaxWidth()) {
        Text("Continue to finish")
    }
}

@Composable
private fun FinishStep(
    draft: OnboardingDraft,
    onKeepManagementApChange: (Boolean) -> Unit,
    onFinish: () -> Unit,
    isLoading: Boolean,
) {
    Text("Choose Management Wi-Fi behavior after setup.", style = MaterialTheme.typography.bodyLarge)
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .selectable(
                selected = !draft.keepManagementApEnabled,
                onClick = { onKeepManagementApChange(false) },
                role = Role.RadioButton,
            ),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        RadioButton(selected = !draft.keepManagementApEnabled, onClick = { onKeepManagementApChange(false) })
        Column(modifier = Modifier.padding(start = 8.dp)) {
            Text("Disable after setup (Recommended)")
            Text(
                "Turns off Management Wi-Fi automatically. Re-enable later from this app.",
                style = MaterialTheme.typography.bodySmall,
            )
        }
    }
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .selectable(
                selected = draft.keepManagementApEnabled,
                onClick = { onKeepManagementApChange(true) },
                role = Role.RadioButton,
            ),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        RadioButton(selected = draft.keepManagementApEnabled, onClick = { onKeepManagementApChange(true) })
        Column(modifier = Modifier.padding(start = 8.dp)) {
            Text("Keep enabled")
            Text(
                "Only if you will manage this appliance frequently.",
                style = MaterialTheme.typography.bodySmall,
            )
        }
    }
    Button(onClick = onFinish, enabled = !isLoading, modifier = Modifier.fillMaxWidth()) {
        Text("Complete setup")
    }
}

private fun stepTitle(step: WizardStep): String = when (step) {
    WizardStep.Welcome -> "Setup"
    WizardStep.NetworkType -> "Network setup"
    WizardStep.RouterConnection -> "Gateway connection"
    WizardStep.PortalConfiguration -> "Portal"
    WizardStep.CoinConfiguration -> "Coin slot"
    WizardStep.Validation -> "Validation"
    WizardStep.Summary -> "Summary"
    WizardStep.Finish -> "Finish"
}
