package com.renzfi.owner.ui.navigation

import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.navigation.NavHostController
import androidx.navigation.NavType
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.rememberNavController
import androidx.navigation.navArgument
import com.renzfi.owner.model.ConnectionState
import com.renzfi.owner.ui.screens.AboutScreen
import com.renzfi.owner.ui.screens.ConnectionHelpScreen
import com.renzfi.owner.ui.screens.DashboardScreen
import com.renzfi.owner.ui.screens.DeviceFormScreen
import com.renzfi.owner.ui.screens.DeviceListScreen
import com.renzfi.owner.ui.screens.DeviceOverviewScreen
import com.renzfi.owner.ui.screens.SettingsScreen
import com.renzfi.owner.ui.screens.SplashScreen
import com.renzfi.owner.viewmodel.DeviceFormViewModel
import com.renzfi.owner.viewmodel.DeviceListViewModel
import com.renzfi.owner.viewmodel.DeviceOverviewViewModel
import com.renzfi.owner.viewmodel.MainViewModel
import com.renzfi.owner.viewmodel.SettingsViewModel

object Routes {
    const val SPLASH = "splash"
    const val DEVICE_LIST = "device_list"
    const val DEVICE_OVERVIEW = "device_overview/{deviceId}"
    const val DEVICE_FORM = "device_form?deviceId={deviceId}"
    const val DASHBOARD = "dashboard/{deviceId}"
    const val CONNECTION_HELP = "connection_help/{deviceId}"
    const val SETTINGS = "settings"
    const val ABOUT = "about"

    fun deviceOverview(deviceId: String) = "device_overview/$deviceId"
    fun deviceForm(deviceId: String? = null) =
        if (deviceId.isNullOrBlank()) "device_form" else "device_form?deviceId=$deviceId"
    fun dashboard(deviceId: String) = "dashboard/$deviceId"
    fun connectionHelp(deviceId: String) = "connection_help/$deviceId"
}

@Composable
fun RenzFiNavGraph(
    navController: NavHostController = rememberNavController(),
    mainViewModel: MainViewModel = viewModel(),
    deviceListViewModel: DeviceListViewModel = viewModel(),
    settingsViewModel: SettingsViewModel = viewModel(),
) {
    val connectionState by mainViewModel.connectionState.collectAsState()
    val activeDevice by mainViewModel.activeDevice.collectAsState()
    val deviceListUiState by deviceListViewModel.uiState.collectAsState()
    val settingsUiState by settingsViewModel.uiState.collectAsState()

    LaunchedEffect(Unit) {
        mainViewModel.startHealthCheck()
    }

    LaunchedEffect(connectionState) {
        when (val state = connectionState) {
            is ConnectionState.Connected -> {
                val route = Routes.dashboard(state.device.id)
                if (navController.currentDestination?.route != Routes.DASHBOARD &&
                    navController.currentDestination?.route != route
                ) {
                    navController.navigate(route) {
                        popUpTo(Routes.SPLASH) { inclusive = true }
                    }
                }
            }
            is ConnectionState.MultiDevice -> {
                if (navController.currentDestination?.route != Routes.DEVICE_LIST &&
                    navController.currentDestination?.route != Routes.SETTINGS
                ) {
                    navController.navigate(Routes.DEVICE_LIST) {
                        popUpTo(Routes.SPLASH) { inclusive = true }
                    }
                }
            }
            is ConnectionState.Failed -> {
                val deviceId = state.device?.id
                if (deviceId != null) {
                    val route = Routes.connectionHelp(deviceId)
                    if (navController.currentDestination?.route != Routes.CONNECTION_HELP &&
                        navController.currentDestination?.route != route &&
                        navController.currentDestination?.route != Routes.SETTINGS &&
                        navController.currentDestination?.route != Routes.DEVICE_LIST
                    ) {
                        navController.navigate(route) {
                            popUpTo(Routes.SPLASH) { inclusive = true }
                        }
                    }
                } else if (navController.currentDestination?.route != Routes.DEVICE_LIST &&
                    navController.currentDestination?.route != Routes.SETTINGS
                ) {
                    navController.navigate(Routes.DEVICE_LIST) {
                        popUpTo(Routes.SPLASH) { inclusive = true }
                    }
                }
            }
            else -> Unit
        }
    }

    NavHost(
        navController = navController,
        startDestination = Routes.SPLASH,
    ) {
        composable(Routes.SPLASH) {
            SplashScreen(
                isChecking = connectionState is ConnectionState.Checking ||
                    connectionState is ConnectionState.Idle,
            )
        }

        composable(Routes.DEVICE_LIST) {
            DeviceListScreen(
                uiState = deviceListUiState,
                onRefresh = deviceListViewModel::refreshStatus,
                onOpenDashboard = { device ->
                    deviceListViewModel.setCheckingDevice(device.id)
                    mainViewModel.checkAndOpenDevice(device.id) {
                        deviceListViewModel.clearCheckingDevice()
                    }
                },
                onEditDevice = { device ->
                    navController.navigate(Routes.deviceForm(device.id))
                },
                onDeleteDevice = { device ->
                    deviceListViewModel.deleteDevice(device.id)
                },
                onDeviceOverview = { device ->
                    navController.navigate(Routes.deviceOverview(device.id))
                },
                onAddDevice = {
                    navController.navigate(Routes.deviceForm())
                },
                onOpenSettings = {
                    navController.navigate(Routes.SETTINGS)
                },
            )
        }

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
                onSave = formViewModel::save,
                onBack = { navController.popBackStack() },
                onSaveSuccessShown = formViewModel::clearSaveSuccess,
            )
        }

        composable(
            route = Routes.DASHBOARD,
            arguments = listOf(navArgument("deviceId") { type = NavType.StringType }),
        ) { backStackEntry ->
            val deviceId = backStackEntry.arguments?.getString("deviceId") ?: return@composable
            val device = activeDevice?.takeIf { it.id == deviceId }
                ?: deviceListUiState.devices.find { it.id == deviceId }

            if (device != null) {
                DashboardScreen(
                    device = device,
                    showDevicesAction = deviceListUiState.devices.size > 1,
                    onOpenSettings = { navController.navigate(Routes.SETTINGS) },
                    onOpenDevices = {
                        navController.navigate(Routes.DEVICE_LIST) {
                            popUpTo(Routes.DASHBOARD) { inclusive = true }
                        }
                    },
                )
            }
        }

        composable(
            route = Routes.CONNECTION_HELP,
            arguments = listOf(navArgument("deviceId") { type = NavType.StringType }),
        ) { backStackEntry ->
            val deviceId = backStackEntry.arguments?.getString("deviceId") ?: return@composable
            val failed = connectionState as? ConnectionState.Failed
            val device = failed?.device
                ?: deviceListUiState.devices.find { it.id == deviceId }

            ConnectionHelpScreen(
                device = device,
                errorMessage = failed?.message,
                onRetry = {
                    if (device != null) {
                        mainViewModel.checkAndOpenDevice(device.id) { }
                    } else {
                        mainViewModel.retry()
                        navController.navigate(Routes.SPLASH) {
                            popUpTo(Routes.CONNECTION_HELP) { inclusive = true }
                        }
                    }
                },
                onOpenSettings = { navController.navigate(Routes.SETTINGS) },
                onBackToDevices = if (deviceListUiState.devices.size > 1) {
                    {
                        navController.navigate(Routes.DEVICE_LIST) {
                            popUpTo(Routes.CONNECTION_HELP) { inclusive = true }
                        }
                    }
                } else {
                    null
                },
            )
        }

        composable(Routes.SETTINGS) {
            SettingsScreen(
                uiState = settingsUiState,
                onAddDevice = { navController.navigate(Routes.deviceForm()) },
                onEditDevice = { device ->
                    navController.navigate(Routes.deviceForm(device.id))
                },
                onDeleteDevice = { device ->
                    settingsViewModel.deleteDevice(device.id)
                },
                onAbout = { navController.navigate(Routes.ABOUT) },
                onBack = { navController.popBackStack() },
            )
        }

        composable(Routes.ABOUT) {
            AboutScreen(onBack = { navController.popBackStack() })
        }
    }
}
