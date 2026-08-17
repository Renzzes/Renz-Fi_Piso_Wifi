package com.renzfi.owner.ui.screens

import android.webkit.WebView
import androidx.activity.compose.BackHandler
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
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Lock
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.Router
import androidx.compose.material.icons.filled.Wifi
import androidx.compose.material.icons.filled.WifiOff
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.Checkbox
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import com.renzfi.owner.R
import com.renzfi.owner.data.repository.DeviceRepository
import com.renzfi.owner.model.VendoDevice
import com.renzfi.owner.ui.components.RenzFiTopBar
import com.renzfi.owner.util.NetworkUtils
import com.renzfi.owner.webview.RenzFiWebView
import kotlinx.coroutines.CancellationException

private enum class ProbeStatus { Checking, Online, Offline, Misconfigured }
private enum class LoginView { Form, WebView }

/**
 * Native Admin Login screen — the owner-facing entry point to the appliance
 * Admin Dashboard.
 *
 * Flow:
 *  1. Screen always opens with the native form (never skips to WebView).
 *  2. A bounded probe is run automatically to check reachability.
 *  3. The connection banner reflects the probe result.
 *  4. "Connect to Admin" is enabled only when the appliance is reachable.
 *  5. Only after an explicit tap on "Connect to Admin" is the WebView created.
 *  6. Back from WebView → native form. Back from form → My Vendo.
 */
@Composable
fun AdminLoginScreen(
    device: VendoDevice,
    showDevicesAction: Boolean,
    onBack: () -> Unit,
    onOpenSettings: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val context = androidx.compose.ui.platform.LocalContext.current
    val repository = remember { DeviceRepository(context.applicationContext) }

    val hostValid = NetworkUtils.isValidHost(device.esp32LocalIp)
    val adminUrl = remember(device.esp32LocalIp) {
        if (hostValid) NetworkUtils.buildAdminUrl(device.esp32LocalIp) else ""
    }

    var currentView by remember { mutableStateOf(LoginView.Form) }
    var probeStatus by remember(device.id) {
        mutableStateOf(if (hostValid) ProbeStatus.Checking else ProbeStatus.Misconfigured)
    }
    var probeTrigger by remember(device.id) { mutableIntStateOf(0) }
    var webViewRef by remember { mutableStateOf<WebView?>(null) }

    // Editable IP — pre-filled from saved device, editable by owner.
    var adminIp by rememberSaveable(device.id) { mutableStateOf(device.esp32LocalIp) }
    var adminPassword by rememberSaveable { mutableStateOf("") }
    var rememberIp by rememberSaveable { mutableStateOf(true) }

    // Run bounded reachability probe whenever triggered (initial load or Retry).
    LaunchedEffect(device.id, hostValid, probeTrigger) {
        if (!hostValid) {
            probeStatus = ProbeStatus.Misconfigured
            return@LaunchedEffect
        }
        probeStatus = ProbeStatus.Checking
        try {
            val result = repository.checkDeviceHealth(device)
            probeStatus = if (result.isOnline) ProbeStatus.Online else ProbeStatus.Offline
        } catch (e: CancellationException) {
            throw e
        } catch (_: Exception) {
            probeStatus = ProbeStatus.Offline
        }
    }

    BackHandler {
        when {
            currentView == LoginView.WebView -> {
                val wv = webViewRef
                if (wv != null && wv.canGoBack()) {
                    wv.goBack()
                } else {
                    currentView = LoginView.Form
                }
            }
            else -> onBack()
        }
    }

    Scaffold(
        modifier = modifier,
        topBar = {
            RenzFiTopBar(
                title = device.name,
                onBack = {
                    when {
                        currentView == LoginView.WebView -> currentView = LoginView.Form
                        else -> onBack()
                    }
                },
                actions = {
                    IconButton(onClick = onOpenSettings) {
                        Icon(
                            imageVector = Icons.Default.Router,
                            contentDescription = "Settings",
                            tint = androidx.compose.ui.graphics.Color.White,
                        )
                    }
                },
            )
        },
    ) { padding ->
        when (currentView) {
            LoginView.Form -> {
                AdminLoginForm(
                    device = device,
                    adminIp = adminIp,
                    onAdminIpChange = { adminIp = it },
                    adminPassword = adminPassword,
                    onAdminPasswordChange = { adminPassword = it },
                    rememberIp = rememberIp,
                    onRememberIpChange = { rememberIp = it },
                    probeStatus = probeStatus,
                    onRetryProbe = { probeTrigger++ },
                    onConnectToAdmin = { currentView = LoginView.WebView },
                    modifier = Modifier
                        .fillMaxSize()
                        .padding(padding),
                )
            }
            LoginView.WebView -> {
                if (adminUrl.isNotBlank()) {
                    RenzFiWebView(
                        url = adminUrl,
                        modifier = Modifier
                            .fillMaxSize()
                            .padding(padding),
                        onWebViewReady = { webViewRef = it },
                        onError = {
                            probeStatus = ProbeStatus.Offline
                            currentView = LoginView.Form
                        },
                    )
                } else {
                    // Defensive: adminUrl should never be blank here since we only enter
                    // WebView when online and hostValid, but guard against race conditions.
                    currentView = LoginView.Form
                }
            }
        }
    }
}

@Composable
private fun AdminLoginForm(
    device: VendoDevice,
    adminIp: String,
    onAdminIpChange: (String) -> Unit,
    adminPassword: String,
    onAdminPasswordChange: (String) -> Unit,
    rememberIp: Boolean,
    onRememberIpChange: (Boolean) -> Unit,
    probeStatus: ProbeStatus,
    onRetryProbe: () -> Unit,
    onConnectToAdmin: () -> Unit,
    modifier: Modifier = Modifier,
) {
    Column(
        modifier = modifier
            .verticalScroll(rememberScrollState())
            .padding(horizontal = 24.dp, vertical = 16.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        // ── Header ────────────────────────────────────────────────────────────
        Card(
            shape = RoundedCornerShape(16.dp),
            elevation = CardDefaults.cardElevation(defaultElevation = 4.dp),
            colors = CardDefaults.cardColors(
                containerColor = MaterialTheme.colorScheme.surface,
            ),
        ) {
            Image(
                painter = painterResource(id = R.drawable.renzfi_logo),
                contentDescription = "Renz-Fi logo",
                modifier = Modifier
                    .padding(16.dp)
                    .size(72.dp),
            )
        }
        Spacer(modifier = Modifier.height(20.dp))
        Text(
            text = "Welcome",
            style = MaterialTheme.typography.headlineMedium,
            fontWeight = FontWeight.Bold,
            color = MaterialTheme.colorScheme.primary,
        )
        Spacer(modifier = Modifier.height(6.dp))
        Text(
            text = "Enter the admin IP address to open the dashboard.",
            style = MaterialTheme.typography.bodyMedium,
            textAlign = TextAlign.Center,
            color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.7f),
        )
        Spacer(modifier = Modifier.height(20.dp))

        // ── Connection banner ─────────────────────────────────────────────────
        ConnectionBanner(probeStatus = probeStatus, onRetryProbe = onRetryProbe)

        Spacer(modifier = Modifier.height(20.dp))

        // ── Form fields ───────────────────────────────────────────────────────
        OutlinedTextField(
            value = adminIp,
            onValueChange = onAdminIpChange,
            label = { Text("Admin IP Address") },
            placeholder = { Text("e.g. 10.40.0.2") },
            singleLine = true,
            keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Uri),
            modifier = Modifier.fillMaxWidth(),
        )
        Spacer(modifier = Modifier.height(12.dp))
        OutlinedTextField(
            value = adminPassword,
            onValueChange = onAdminPasswordChange,
            label = { Text("Admin Password") },
            placeholder = { Text("Enter password") },
            singleLine = true,
            visualTransformation = PasswordVisualTransformation(),
            leadingIcon = {
                Icon(imageVector = Icons.Default.Lock, contentDescription = null)
            },
            modifier = Modifier.fillMaxWidth(),
        )
        Spacer(modifier = Modifier.height(8.dp))
        Row(
            modifier = Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Checkbox(
                checked = rememberIp,
                onCheckedChange = onRememberIpChange,
            )
            Text(
                text = "Remember IP address on this device",
                style = MaterialTheme.typography.bodyMedium,
            )
        }
        Spacer(modifier = Modifier.height(24.dp))

        // ── Connect button ────────────────────────────────────────────────────
        Button(
            onClick = onConnectToAdmin,
            enabled = probeStatus == ProbeStatus.Online,
            modifier = Modifier.fillMaxWidth(),
        ) {
            Text("Connect to Admin")
        }

        if (probeStatus == ProbeStatus.Misconfigured) {
            Spacer(modifier = Modifier.height(8.dp))
            Text(
                text = "Device configuration is incomplete. Go to Settings → Device Management and edit this appliance to add a valid IP address.",
                style = MaterialTheme.typography.bodySmall,
                textAlign = TextAlign.Center,
                color = MaterialTheme.colorScheme.error,
            )
        }
    }
}

@Composable
private fun ConnectionBanner(
    probeStatus: ProbeStatus,
    onRetryProbe: () -> Unit,
) {
    val (icon, text, containerColor, contentColor) = when (probeStatus) {
        ProbeStatus.Checking -> BannerStyle(
            icon = null,
            text = "Checking connection…",
            container = MaterialTheme.colorScheme.surfaceVariant,
            content = MaterialTheme.colorScheme.onSurfaceVariant,
        )
        ProbeStatus.Online -> BannerStyle(
            icon = Icons.Default.Wifi,
            text = "Appliance connected. You can sign in to Admin.",
            container = MaterialTheme.colorScheme.primaryContainer,
            content = MaterialTheme.colorScheme.onPrimaryContainer,
        )
        ProbeStatus.Offline -> BannerStyle(
            icon = Icons.Default.WifiOff,
            text = "Appliance offline. Connect your phone to the same Renz-Fi network to sign in.",
            container = MaterialTheme.colorScheme.errorContainer,
            content = MaterialTheme.colorScheme.onErrorContainer,
        )
        ProbeStatus.Misconfigured -> BannerStyle(
            icon = Icons.Default.WifiOff,
            text = "Invalid IP address — cannot reach appliance.",
            container = MaterialTheme.colorScheme.errorContainer,
            content = MaterialTheme.colorScheme.onErrorContainer,
        )
    }

    Surface(
        color = containerColor,
        shape = RoundedCornerShape(12.dp),
        modifier = Modifier.fillMaxWidth(),
    ) {
        Row(
            modifier = Modifier.padding(horizontal = 16.dp, vertical = 12.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(10.dp),
        ) {
            if (probeStatus == ProbeStatus.Checking) {
                CircularProgressIndicator(
                    modifier = Modifier.size(20.dp),
                    strokeWidth = 2.dp,
                    color = contentColor,
                )
            } else if (icon != null) {
                Icon(
                    imageVector = icon,
                    contentDescription = null,
                    tint = contentColor,
                    modifier = Modifier.size(20.dp),
                )
            }
            Text(
                text = text,
                style = MaterialTheme.typography.bodySmall,
                color = contentColor,
                modifier = Modifier.weight(1f),
            )
            if (probeStatus == ProbeStatus.Offline || probeStatus == ProbeStatus.Checking) {
                IconButton(onClick = onRetryProbe, modifier = Modifier.size(32.dp)) {
                    Icon(
                        imageVector = Icons.Default.Refresh,
                        contentDescription = "Retry connection",
                        tint = contentColor,
                    )
                }
            }
        }
    }
}

private data class BannerStyle(
    val icon: androidx.compose.ui.graphics.vector.ImageVector?,
    val text: String,
    val container: androidx.compose.ui.graphics.Color,
    val content: androidx.compose.ui.graphics.Color,
)
