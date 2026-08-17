package com.renzfi.owner.ui.screens.onboarding

import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleEventObserver
import androidx.lifecycle.compose.LocalLifecycleOwner
import androidx.lifecycle.viewmodel.compose.viewModel
import com.renzfi.owner.model.VendoDevice
import com.renzfi.owner.viewmodel.AddApplianceViewModel
import com.renzfi.owner.viewmodel.OnboardingPhase
import com.renzfi.owner.viewmodel.OnboardingViewModel

@Composable
fun OnboardingHostScreen(
    smartScan: Boolean,
    onFinished: (VendoDevice) -> Unit,
    onAddExisting: () -> Unit,
    onCancel: () -> Unit,
    modifier: Modifier = Modifier,
    viewModel: OnboardingViewModel = viewModel(),
) {
    val uiState by viewModel.uiState.collectAsState()
    val lifecycleOwner = LocalLifecycleOwner.current

    LaunchedEffect(smartScan) {
        if (smartScan) {
            viewModel.detectAppliance(autoAdvance = true)
        }
    }

    DisposableEffect(lifecycleOwner) {
        val observer = LifecycleEventObserver { _, event ->
            if (event == Lifecycle.Event.ON_RESUME) {
                viewModel.onReturnedFromWifiSettings()
            }
        }
        lifecycleOwner.lifecycle.addObserver(observer)
        onDispose { lifecycleOwner.lifecycle.removeObserver(observer) }
    }

    when (uiState.phase) {
        OnboardingPhase.WifiInstructions,
        OnboardingPhase.Detecting,
        OnboardingPhase.Detected,
        -> OnboardingConnectScreen(
            modifier = modifier,
            phase = uiState.phase,
            detected = uiState.detected,
            isLoading = uiState.isLoading,
            errorMessage = uiState.errorMessage,
            onOpenWifiSettings = viewModel::onOpenManagementWifiSettings,
            onRetryDetect = viewModel::detectAppliance,
            onContinue = viewModel::continueFromDetection,
            onCancel = {
                viewModel.cancelOnboarding()
                onCancel()
            },
            onBack = {
                viewModel.cancelOnboarding()
                onCancel()
            },
        )

        OnboardingPhase.Wizard -> OnboardingWizardScreen(
            modifier = modifier,
            wizardStep = uiState.wizardStep,
            workflow = uiState.workflow,
            draft = uiState.draft,
            networkStatus = uiState.networkStatus,
            validationChecks = uiState.validationChecks,
            validationPassed = uiState.validationPassed,
            isLoading = uiState.isLoading,
            errorMessage = uiState.errorMessage,
            onBack = onCancel,
            onCancel = {
                viewModel.cancelOnboarding()
                onCancel()
            },
            onWelcomeContinue = viewModel::startOrResumeWizard,
            onNetworkModeChange = { mode ->
                viewModel.updateDraft { it.copy(networkMode = mode) }
            },
            onNetworkTypeSubmit = viewModel::submitNetworkType,
            onRouterHostChange = { host -> viewModel.updateDraft { it.copy(routerHost = host) } },
            onRouterUsernameChange = { user -> viewModel.updateDraft { it.copy(routerUsername = user) } },
            onRouterPasswordChange = { pass -> viewModel.updateDraft { it.copy(routerPassword = pass) } },
            onRouterSubmit = viewModel::submitRouterConnection,
            onPortalSubmit = viewModel::submitPortalConfiguration,
            onCoinSubmit = { viewModel.submitCoinConfiguration(skip = false) },
            onCoinSkip = { viewModel.submitCoinConfiguration(skip = true) },
            onValidate = viewModel::runValidation,
            onSummaryContinue = viewModel::goToFinishStep,
            onKeepManagementApChange = { keep ->
                viewModel.updateDraft { it.copy(keepManagementApEnabled = keep) }
            },
            onFinish = viewModel::completeSetup,
        )

        OnboardingPhase.RejoinWifi,
        OnboardingPhase.Discovering,
        OnboardingPhase.Complete,
        -> {
            val device = uiState.registeredDevice
            if (uiState.phase == OnboardingPhase.Complete && device != null) {
                OnboardingRejoinScreen(
                    modifier = modifier,
                    phase = uiState.phase,
                    registeredDevice = device,
                    registeredIp = uiState.registeredIp,
                    preferredWifiSsid = uiState.preferredWifiSsid,
                    isLoading = false,
                    errorMessage = null,
                    discoveryProgress = null,
                    onOpenWifiSettings = { },
                    onRetryDiscovery = { },
                    onOpenDashboard = onFinished,
                    onCancel = onCancel,
                )
            } else {
                OnboardingRejoinScreen(
                    modifier = modifier,
                    phase = uiState.phase,
                    registeredDevice = uiState.registeredDevice,
                    registeredIp = uiState.registeredIp,
                    preferredWifiSsid = uiState.preferredWifiSsid,
                    isLoading = uiState.isLoading,
                    errorMessage = uiState.errorMessage,
                    discoveryProgress = uiState.discoveryProgress,
                    onOpenWifiSettings = viewModel::onOpenLanWifiSettings,
                    onRetryDiscovery = viewModel::rediscoverOnLan,
                    onOpenDashboard = onFinished,
                    onCancel = {
                        viewModel.cancelOnboarding()
                        onCancel()
                    },
                )
            }
        }
    }
}

@Composable
fun AddApplianceHostScreen(
    onStartOnboarding: (smartScan: Boolean) -> Unit,
    onAlreadyRegistered: (deviceId: String) -> Unit,
    onAddExisting: () -> Unit,
    onBack: () -> Unit,
    modifier: Modifier = Modifier,
    mainViewModel: com.renzfi.owner.viewmodel.MainViewModel,
    viewModel: AddApplianceViewModel = viewModel(),
) {
    val uiState by viewModel.uiState.collectAsState()
    val lifecycleOwner = LocalLifecycleOwner.current

    LaunchedEffect(Unit) {
        viewModel.resetNavigationGate()
        viewModel.scanNearby()
    }

    LaunchedEffect(uiState.navigateToOnboarding) {
        if (uiState.navigateToOnboarding) {
            viewModel.consumeNavigation()
            onStartOnboarding(true)
        }
    }

    LaunchedEffect(uiState.pendingAlreadyRegistered) {
        val evaluation = uiState.pendingAlreadyRegistered ?: return@LaunchedEffect
        viewModel.consumeAlreadyRegisteredNavigation()
        mainViewModel.setStartupAlreadyRegistered(evaluation.device, evaluation.info)
        onAlreadyRegistered(evaluation.device.id)
    }

    DisposableEffect(lifecycleOwner) {
        val observer = LifecycleEventObserver { _, event ->
            if (event == Lifecycle.Event.ON_RESUME) {
                viewModel.onScreenResumed()
            }
        }
        lifecycleOwner.lifecycle.addObserver(observer)
        onDispose { lifecycleOwner.lifecycle.removeObserver(observer) }
    }

    AddApplianceScreen(
        modifier = modifier,
        uiState = uiState,
        onScanNearby = viewModel::scanNearby,
        onOpenWifiSettings = viewModel::onOpenWifiSettings,
        onAddExisting = onAddExisting,
        onBack = onBack,
    )
}
