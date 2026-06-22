package com.renzfi.owner.ui.screens

import android.app.Activity
import android.webkit.WebView
import androidx.activity.compose.BackHandler
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Devices
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.Scaffold
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import com.renzfi.owner.model.VendoDevice
import com.renzfi.owner.ui.components.RenzFiTopBar
import com.renzfi.owner.util.NetworkUtils
import com.renzfi.owner.webview.RenzFiWebView

@Composable
fun DashboardScreen(
    device: VendoDevice,
    showDevicesAction: Boolean,
    onOpenSettings: () -> Unit,
    onOpenDevices: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val context = LocalContext.current
    var webView by remember { mutableStateOf<WebView?>(null) }
    val adminUrl = NetworkUtils.buildAdminUrl(device.esp32LocalIp)

    BackHandler {
        val view = webView
        if (view != null && view.canGoBack()) {
            view.goBack()
        } else {
            (context as? Activity)?.moveTaskToBack(true)
        }
    }

    Scaffold(
        modifier = modifier,
        topBar = {
            RenzFiTopBar(
                title = device.name,
                actions = {
                    if (showDevicesAction) {
                        IconButton(onClick = onOpenDevices) {
                            Icon(
                                imageVector = Icons.Default.Devices,
                                contentDescription = "Devices",
                                tint = Color.White,
                            )
                        }
                    }
                    IconButton(onClick = onOpenSettings) {
                        Icon(
                            imageVector = Icons.Default.Settings,
                            contentDescription = "Settings",
                            tint = Color.White,
                        )
                    }
                },
            )
        },
    ) { padding ->
        RenzFiWebView(
            url = adminUrl,
            modifier = Modifier
                .fillMaxSize()
                .padding(padding),
            onWebViewReady = { webView = it },
        )
    }
}
