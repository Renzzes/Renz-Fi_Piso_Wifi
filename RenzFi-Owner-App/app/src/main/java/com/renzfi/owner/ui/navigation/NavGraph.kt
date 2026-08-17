package com.renzfi.owner.ui.navigation

import android.content.Intent
import android.net.Uri
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.navigation.NavHostController
import androidx.navigation.NavType
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.rememberNavController
import androidx.navigation.navArgument
import com.renzfi.owner.model.StartupDiscovery
import com.renzfi.owner.update.UpdateState
import com.renzfi.owner.ui.screens.AboutScreen
import com.renzfi.owner.ui.screens.AdminLoginScreen
import com.renzfi.owner.ui.screens.ConnectionHelpScreen
import com.renzfi.owner.ui.screens.DashboardFallbackScreen
import com.renzfi.owner.ui.screens.DeviceFormScreen
import com.renzfi.owner.ui.screens.DeviceListScreen
import com.renzfi.owner.ui.screens.DeviceOverviewScreen
import com.renzfi.owner.ui.screens.SettingsScreen
import com.renzfi.owner.ui.screens.SplashScreen
import com.renzfi.owner.ui.screens.onboarding.AddApplianceHostScreen
import com.renzfi.owner.ui.screens.onboarding.AlreadyRegisteredSetupScreen
import com.renzfi.owner.ui.screens.onboarding.OnboardingHostScreen
import com.renzfi.owner.util.Constants
import com.renzfi.owner.viewmodel.AboutViewModel
import com.renzfi.owner.viewmodel.DeviceFormViewModel
import com.renzfi.owner.viewmodel.DeviceListViewModel
import com.renzfi.owner.viewmodel.DeviceOverviewViewModel
import com.renzfi.owner.viewmodel.MainViewModel
import com.renzfi.owner.viewmodel.SettingsViewModel
import com.renzfi.owner.viewmodel.StartupRoutingResult

object Routes {
    const val SPLASH = "splash"
    const val DEVICE_LIST = "device_list"
    const val APPLIANCE_NOT_FOUND = "appliance_not_found"
    const val DEVICE_OVERVIEW = "device_overview/{deviceId}"
    const val DEVICE_FORM = "device_form?deviceId={deviceId}"
    const val ADMIN_LOGIN = "admin_login/{deviceId}"
    const val SETTINGS = "settings"
    const val ABOUT = "about"
    const val ADD_APPLIANCE = "add_appliance"
    const val SETUP_ALREADY_REGISTERED = "setup_already_registered/{deviceId}"
    const val ONBOARDING_SETUP = "onboarding_setup?smartScan={smartScan}"

    fun deviceOverview(deviceId: String) = "device_overview/$deviceId"
    fun deviceForm(deviceId: String? = null) =
        if (deviceId.isNullOrBlank()) "device_form" else "device_form?deviceId=$deviceId"
    fun adminLogin(deviceId: String) = "admin_login/$deviceId"
    fun onboardingSetup(smartScan: Boolean = false) = "onboarding_setup?smartScan=$smartScan"
    fun setupAlreadyRegistered(deviceId: String) = "setup_already_registered/$deviceId"
}

@Composable
fun RenzFiNavGraph(
    navController: NavHostController = rememberNavController(),
    mainViewModel: MainViewModel = viewModel(),
    deviceListViewModel: DeviceListViewModel = viewModel(),
    settingsViewModel: SettingsViewModel = viewModel(),
) {
    val startupResult by mainViewModel.startupResult.collectAsState()
    val startupDiscovery by mainViewModel.startupDiscovery.collectAsState()
    val activeDevice by mainViewModel.activeDevice.collectAsState()
    val deviceListUiState by deviceListViewModel.uiState.collectAsState()
    val settingsUiState by settingsViewModel.uiState.collectAsState()

    // ── Startup routing ───────────────────────────────────────────────────────
    // Drives the Mode A / Mode B navigation out of the splash screen.
    // Runs exactly once per startup; after the NavGraph has moved off SPLASH,
    // none of these states trigger further automatic navigation.
    LaunchedEffect(startupResult) {
        val current = navController.currentDestination?.route
        if (current != Routes.SPLASH) return@LaunchedEffect

        when (startupResult) {
            StartupRoutingResult.HasRegisteredDevices -> {
                navController.navigate(Routes.DEVICE_LIST) {
                    popUpTo(Routes.SPLASH) { inclusive = true }
                }
            }
            StartupRoutingResult.NoDevicesApplianceNotFound -> {
                navController.navigate(Routes.APPLIANCE_NOT_FOUND) {
                    popUpTo(Routes.SPLASH) { inclusive = true }
                }
            }
            StartupRoutingResult.Idle,
            StartupRoutingResult.Checking,
            -> Unit
        }
    }

    Box(modifier = Modifier.fillMaxSize()) {
        NavHost(
            navController = navController,
            startDestination = Routes.SPLASH,
        ) {

            // ── Splash ────────────────────────────────────────────────────────
            composable(Routes.SPLASH) {
                LaunchedEffect(Unit) {
                    mainViewModel.performStartupCheck()
                }
                SplashScreen(isChecking = true)
            }

            // ── Mode A: no registered devices, appliance not found ────────────
            composable(Routes.APPLIANCE_NOT_FOUND) {
                ConnectionHelpScreen(
                    device = null,
                    errorMessage = null,
                    onRetry = {
                        mainViewModel.resetStartupCheck()
                        navController.navigate(Routes.SPLASH) {
                            popUpTo(Routes.APPLIANCE_NOT_FOUND) { inclusive = true }
                        }
                    },
                    onOpenSettings = { navController.navigate(Routes.SETTINGS) },
                    onBackToDevices = null,
                )
            }

            // ── Mode B: My Vendo ──────────────────────────────────────────────
            composable(Routes.DEVICE_LIST) {
                LaunchedEffect(Unit) {
                    deviceListViewModel.startBackgroundRefresh()
                    mainViewModel.scheduleBackgroundDiscovery()
                }

                DeviceListScreen(
                    uiState = deviceListUiState,
                    onRefresh = deviceListViewModel::refreshStatus,
                    onLoginToAdmin = { device ->
                        navController.navigate(Routes.adminLogin(device.id))
                    },
                    onEditDevice = { device ->
                        navController.navigate(Routes.deviceForm(device.id))
                    },
                    onDeleteDevice = { device ->
                        deviceListViewModel.deleteDevice(device.id)
                    },
                    onAddDevice = {
                        navController.navigate(Routes.ADD_APPLIANCE)
                    },
                    onOpenSettings = {
                        navController.navigate(Routes.SETTINGS)
                    },
                )
            }

            // ── Admin Login (native login + WebView gateway) ───────────────────
            composable(
                route = Routes.ADMIN_LOGIN,
                arguments = listOf(navArgument("deviceId") { type = NavType.StringType }),
            ) { backStackEntry ->
                val deviceId = backStackEntry.arguments?.getString("deviceId")
                if (deviceId.isNullOrBlank()) {
                    DashboardFallbackScreen(onBack = { navController.popBackStack() })
                    return@composable
                }
                val device = activeDevice?.takeIf { it.id == deviceId }
                    ?: deviceListUiState.devices.find { it.id == deviceId }
                    ?: settingsUiState.devices.find { it.id == deviceId }

                if (device != null) {
                    AdminLoginScreen(
                        device = device,
                        showDevicesAction = deviceListUiState.devices.size > 1,
                        onBack = { navController.popBackStack() },
                        onOpenSettings = { navController.navigate(Routes.SETTINGS) },
                    )
                } else {
                    DashboardFallbackScreen(
                        onBack = { navController.popBackStack() },
                        message = "Device configuration is incomplete.",
                    )
                }
            }

            // ── Device Overview (kept for existing use) ───────────────────────
            composable(
                route = Routes.DEVICE_OVERVIEW,
                arguments = listOf(navArgument("deviceId") { type = NavType.StringType }),
            ) { backStackEntry ->
                val deviceId = backStackEntry.arguments?.getString("deviceId") ?: return@composable
                val overviewViewModel: DeviceOverviewViewModel = viewModel()
                val overviewUiState by overviewViewModel.uiState.collectAsState()

                LaunchedEffect(deviceId) {
                    overviewViewModel.loadDevice(deviceId)
                }

                DeviceOverviewScreen(
                    uiState = overviewUiState,
                    onRefresh = { overviewViewModel.refreshDevice(deviceId) },
                    onBack = { navController.popBackStack() },
                )
            }

            // ── Device Form (add / edit) ──────────────────────────────────────
            composable(
                route = Routes.DEVICE_FORM,
                arguments = listOf(
                    navArgument("deviceId") {
                        type = NavType.StringType
                        nullable = true
                        defaultValue = null
                    },
                ),
            ) { backStackEntry ->
                val deviceId = backStackEntry.arguments?.getString("deviceId")
                val formViewModel: DeviceFormViewModel = viewModel()
                val formUiState by formViewModel.uiState.collectAsState()

                LaunchedEffect(deviceId) {
                    formViewModel.loadDevice(deviceId)
                }

                DeviceFormScreen(
                    uiState = formUiState,
                    isEditMode = deviceId != null,
                    onNameChange = formViewModel::updateName,
                    onMikrotikDisplayNameChange = formViewModel::updateMikrotikDisplayName,
                    onMikrotikDdnsChange = formViewModel::updateMikrotikDdns,
                    onMikrotikPublicIpChange = formViewModel::updateMikrotikPublicIp,
                    onMikrotikNotesChange = formViewModel::updateMikrotikNotes,
                    onEsp32LocalIpChange = formViewModel::updateEsp32LocalIp,
                    onDiscoverySubnetChange = formViewModel::updateDiscoverySubnet,
                    onDiscoverSubnet = formViewModel::discoverSubnet,
                    onDiscoverAtIp = formViewModel::discoverAtCurrentIp,
                    onSave = formViewModel::save,
                    onBack = { navController.popBackStack() },
                    onSaveSuccessShown = formViewModel::clearSaveSuccess,
                )
            }

            // ── Setup: already-registered appliance found via setup SSID ──────
            composable(
                route = Routes.SETUP_ALREADY_REGISTERED,
                arguments = listOf(navArgument("deviceId") { type = NavType.StringType }),
            ) { backStackEntry ->
                val context = LocalContext.current
                val deviceId = backStackEntry.arguments?.getString("deviceId") ?: return@composable
                val discovery = startupDiscovery
                val registered = (discovery as? StartupDiscovery.AlreadyRegistered)
                val device = registered?.device?.takeIf { it.id == deviceId }
                    ?: deviceListUiState.devices.find { it.id == deviceId }
                    ?: settingsUiState.devices.find { it.id == deviceId }

                if (device == null) {
                    DashboardFallbackScreen(
                        onBack = { navController.popBackStack() },
                        message = "Device configuration is incomplete.",
                    )
                    return@composable
                }
                val info = registered?.info ?: com.renzfi.owner.model.NearbyApplianceInfo(
                    deviceId = device.applianceDeviceId ?: device.id,
                    firmwareVersion = device.firmwareVersion,
                    hardwareRevision = device.hardwareRevision,
                    buildLabel = device.firmwareVersion,
                    managementApMode = null,
                    needsSetup = false,
                    connectedSsid = null,
                    buildDetails = com.renzfi.owner.model.ApplianceBuildDetails(
                        adminBuild = null,
                        buildNumber = null,
                        gitCommit = null,
                    ),
                    readiness = com.renzfi.owner.model.ApplianceReadinessSummary(
                        hardwareChecks = emptyList(),
                        gatewayProduct = com.renzfi.owner.model.GatewayProductStatus(
                            name = com.renzfi.owner.util.ProductBranding.GATEWAY_NAME,
                            subtitle = com.renzfi.owner.util.ProductBranding.GATEWAY_SUBTITLE,
                            connectionLabel = "",
                            tone = com.renzfi.owner.model.ReadinessTone.Ready,
                        ),
                        gatewayCapabilities = emptyList(),
                        gatewayChecks = emptyList(),
                        readyForInstallation = false,
                        headline = "",
                    ),
                )

                AlreadyRegisteredSetupScreen(
                    device = device,
                    info = info,
                    onOpenDashboard = {
                        mainViewModel.clearStartupDiscovery()
                        mainViewModel.setActiveDevice(device)
                        navController.navigate(Routes.adminLogin(device.id)) {
                            popUpTo(Routes.DEVICE_LIST) { inclusive = false }
                        }
                    },
                    onMaintenanceMode = {
                        mainViewModel.clearStartupDiscovery()
                        mainViewModel.setActiveDevice(device)
                        try {
                            context.startActivity(
                                Intent(
                                    Intent.ACTION_VIEW,
                                    Uri.parse("http://${Constants.MANAGEMENT_AP_IP}/admin"),
                                ).apply { addFlags(Intent.FLAG_ACTIVITY_NEW_TASK) },
                            )
                        } catch (_: Exception) {
                            // Stay in app if no browser is available.
                        }
                    },
                    onBack = {
                        mainViewModel.clearStartupDiscovery()
                        navController.navigate(Routes.DEVICE_LIST) {
                            popUpTo(Routes.SETUP_ALREADY_REGISTERED) { inclusive = true }
                        }
                    },
                )
            }

            // ── Add Appliance ─────────────────────────────────────────────────
            composable(Routes.ADD_APPLIANCE) {
                AddApplianceHostScreen(
                    mainViewModel = mainViewModel,
                    onStartOnboarding = { smartScan ->
                        navController.navigate(Routes.onboardingSetup(smartScan)) {
                            popUpTo(Routes.ADD_APPLIANCE) { inclusive = true }
                        }
                    },
                    onAlreadyRegistered = { deviceId ->
                        navController.navigate(Routes.setupAlreadyRegistered(deviceId)) {
                            popUpTo(Routes.ADD_APPLIANCE) { inclusive = true }
                        }
                    },
                    onAddExisting = {
                        navController.navigate(Routes.deviceForm()) {
                            popUpTo(Routes.ADD_APPLIANCE) { inclusive = true }
                        }
                    },
                    onBack = { navController.popBackStack() },
                )
            }

            // ── Onboarding Setup wizard ───────────────────────────────────────
            composable(
                route = Routes.ONBOARDING_SETUP,
                arguments = listOf(
                    navArgument("smartScan") {
                        type = NavType.BoolType
                        defaultValue = false
                    },
                ),
            ) { backStackEntry ->
                val smartScan = backStackEntry.arguments?.getBoolean("smartScan") ?: false
                OnboardingHostScreen(
                    smartScan = smartScan,
                    onFinished = { device ->
                        deviceListViewModel.refreshStatus()
                        // After successful onboarding, go to My Vendo then Admin Login.
                        navController.navigate(Routes.DEVICE_LIST) {
                            popUpTo(0) { inclusive = true }
                        }
                        navController.navigate(Routes.adminLogin(device.id))
                    },
                    onAddExisting = {
                        navController.navigate(Routes.deviceForm()) {
                            popUpTo(Routes.ONBOARDING_SETUP) { inclusive = true }
                        }
                    },
                    onCancel = {
                        // If devices now exist go to My Vendo; otherwise Appliance Not Found.
                        val hasDevices = deviceListUiState.devices.isNotEmpty()
                        if (hasDevices) {
                            navController.navigate(Routes.DEVICE_LIST) {
                                popUpTo(0) { inclusive = true }
                            }
                        } else {
                            navController.navigate(Routes.APPLIANCE_NOT_FOUND) {
                                popUpTo(0) { inclusive = true }
                            }
                        }
                    },
                )
            }

            // ── Settings ──────────────────────────────────────────────────────
            composable(Routes.SETTINGS) {
                SettingsScreen(
                    uiState = settingsUiState,
                    onAddDevice = { navController.navigate(Routes.ADD_APPLIANCE) },
                    onOpenDevice = { device ->
                        navController.navigate(Routes.adminLogin(device.id))
                    },
                    onEditDevice = { device ->
                        navController.navigate(Routes.deviceForm(device.id))
                    },
                    onDeleteDevice = { device ->
                        settingsViewModel.deleteDevice(device.id)
                    },
                    onCheckForUpdates = { settingsViewModel.checkForUpdates() },
                    onAbout = { navController.navigate(Routes.ABOUT) },
                    onBack = { navController.popBackStack() },
                )
            }

            // ── About ─────────────────────────────────────────────────────────
            composable(Routes.ABOUT) {
                val aboutViewModel: AboutViewModel = viewModel()
                val uiState by aboutViewModel.uiState.collectAsState()
                val context = LocalContext.current

                AboutScreen(
                    uiState = uiState,
                    onBack = { navController.popBackStack() },
                    onCheckForUpdates = aboutViewModel::checkForUpdates,
                    onUpdateNow = aboutViewModel::downloadUpdate,
                    onDismissUpdate = aboutViewModel::dismissUpdate,
                    onCancelDownload = aboutViewModel::cancelDownload,
                    onChannelChange = aboutViewModel::setChannel,
                    onInstallUpdate = {
                        val state = uiState.updateState
                        if (state is UpdateState.ReadyToInstall) {
                            aboutViewModel.installApk(context, state.apkFile)
                        }
                    },
                    onResetError = aboutViewModel::resetError,
                )
            }
        }

        // ── Non-blocking appliance discovery dialogs ──────────────────────────
        // Shown on top of whatever screen is visible. None of these dialogs navigate
        // automatically — the user must explicitly choose an action.
        when (val discovery = startupDiscovery) {
            is StartupDiscovery.NewAppliance -> {
                // Factory appliance (needsSetup=true): offer guided first-time setup.
                // Non-blocking — "Not Now" simply dismisses with no side effects; setup
                // only starts if the user explicitly taps "Set Up Appliance".
                AlertDialog(
                    onDismissRequest = { mainViewModel.dismissStartupDiscovery() },
                    title = { Text("Renz-Fi setup network detected") },
                    text = {
                        Text(
                            "A factory appliance is nearby and ready to set up. " +
                                "You can begin setup now or continue to the app and set it up later.",
                        )
                    },
                    confirmButton = {
                        Button(
                            onClick = {
                                try {
                                    mainViewModel.clearStartupDiscovery()
                                    navController.navigate(Routes.onboardingSetup(smartScan = true))
                                } catch (_: Exception) {
                                    mainViewModel.dismissStartupDiscovery()
                                }
                            },
                        ) {
                            Text("Set Up Appliance")
                        }
                    },
                    dismissButton = {
                        TextButton(onClick = { mainViewModel.dismissStartupDiscovery() }) {
                            Text("Not Now")
                        }
                    },
                )
            }
            is StartupDiscovery.FoundConfiguredAppliance -> {
                // Already-configured appliance found nearby but not yet in the local registry.
                // "Login to Admin" must register the device before opening Admin Login.
                // Route directly into the onboarding wizard (smartScan=true) so it probes,
                // registers, and calls onFinished(device) — the device is saved before
                // adminLogin is ever reached.  Do NOT navigate to adminLogin here directly.
                AlertDialog(
                    onDismissRequest = { mainViewModel.dismissStartupDiscovery() },
                    title = { Text("Renz-Fi Appliance Found") },
                    text = {
                        Text(
                            "A Renz-Fi appliance is nearby but is not yet added to this app. " +
                                "Add it now to manage it and sign in to Admin.",
                        )
                    },
                    confirmButton = {
                        Button(
                            onClick = {
                                try {
                                    mainViewModel.clearStartupDiscovery()
                                    // Go straight to the registration wizard; it probes the
                                    // appliance, saves the device record, then calls
                                    // onFinished(device) which routes to My Vendo + Admin Login.
                                    navController.navigate(Routes.onboardingSetup(smartScan = true))
                                } catch (_: Exception) {
                                    mainViewModel.dismissStartupDiscovery()
                                }
                            },
                        ) {
                            Text("Add & Login to Admin")
                        }
                    },
                    dismissButton = {
                        TextButton(onClick = { mainViewModel.dismissStartupDiscovery() }) {
                            Text("Later")
                        }
                    },
                )
            }
            is StartupDiscovery.AlreadyRegistered -> {
                // Locally-registered appliance reachable on setup SSID.
                AlertDialog(
                    onDismissRequest = { mainViewModel.dismissStartupDiscovery() },
                    title = { Text("Renz-Fi Appliance Nearby") },
                    text = {
                        Text(
                            "You are connected to ${discovery.device.name}'s setup network. " +
                                "Open the Admin Login or dismiss to stay in the app.",
                        )
                    },
                    confirmButton = {
                        Button(
                            onClick = {
                                try {
                                    mainViewModel.clearStartupDiscovery()
                                    mainViewModel.setActiveDevice(discovery.device)
                                    navController.navigate(Routes.adminLogin(discovery.device.id))
                                } catch (_: Exception) {
                                    mainViewModel.dismissStartupDiscovery()
                                }
                            },
                        ) {
                            Text("Login to Admin")
                        }
                    },
                    dismissButton = {
                        TextButton(onClick = { mainViewModel.dismissStartupDiscovery() }) {
                            Text("Later")
                        }
                    },
                )
            }
            StartupDiscovery.None -> Unit
        }
    }
}
