package com.renzfi.owner.ui.screens.onboarding



import android.content.Intent

import android.provider.Settings

import androidx.compose.foundation.layout.Arrangement

import androidx.compose.foundation.layout.Column

import androidx.compose.foundation.layout.fillMaxSize

import androidx.compose.foundation.layout.fillMaxWidth

import androidx.compose.foundation.layout.padding

import androidx.compose.material.icons.Icons

import androidx.compose.material.icons.filled.CheckCircle

import androidx.compose.material3.Button

import androidx.compose.material3.Card

import androidx.compose.material3.CardDefaults

import androidx.compose.material3.CircularProgressIndicator

import androidx.compose.material3.ExperimentalMaterial3Api

import androidx.compose.material3.Icon

import androidx.compose.material3.MaterialTheme

import androidx.compose.material3.OutlinedButton

import androidx.compose.material3.Scaffold

import androidx.compose.material3.Text

import androidx.compose.material3.TopAppBar

import androidx.compose.runtime.Composable

import androidx.compose.ui.Alignment

import androidx.compose.ui.Modifier

import androidx.compose.ui.platform.LocalContext

import androidx.compose.ui.text.font.FontWeight

import androidx.compose.ui.unit.dp

import com.renzfi.owner.model.VendoDevice

import com.renzfi.owner.viewmodel.OnboardingPhase



@OptIn(ExperimentalMaterial3Api::class)

@Composable

fun OnboardingRejoinScreen(

    phase: OnboardingPhase,

    registeredDevice: VendoDevice?,

    registeredIp: String?,

    preferredWifiSsid: String?,

    isLoading: Boolean,

    errorMessage: String?,

    discoveryProgress: String?,

    onOpenWifiSettings: () -> Unit,

    onRetryDiscovery: () -> Unit,

    onOpenDashboard: (VendoDevice) -> Unit,

    onCancel: () -> Unit,

    modifier: Modifier = Modifier,

) {

    val context = LocalContext.current

    val wifiLabel = preferredWifiSsid?.takeIf { it.isNotBlank() } ?: "your normal Wi-Fi"



    Scaffold(

        modifier = modifier,

        topBar = {

            TopAppBar(

                title = {

                    Text(

                        when (phase) {

                            OnboardingPhase.Complete -> "Registration complete"

                            OnboardingPhase.Discovering -> "Scanning network"

                            else -> "Setup complete"

                        },

                    )

                },

            )

        },

    ) { padding ->

        Column(

            modifier = Modifier

                .fillMaxSize()

                .padding(padding)

                .padding(24.dp),

            verticalArrangement = Arrangement.spacedBy(16.dp),

        ) {

            when (phase) {

                OnboardingPhase.Complete -> {

                    Text(

                        "Registration complete",

                        style = MaterialTheme.typography.headlineSmall,

                        fontWeight = FontWeight.SemiBold,

                    )

                    Card(

                        modifier = Modifier.fillMaxWidth(),

                        colors = CardDefaults.cardColors(

                            containerColor = MaterialTheme.colorScheme.primaryContainer,

                        ),

                    ) {

                        Column(

                            modifier = Modifier.padding(20.dp),

                            verticalArrangement = Arrangement.spacedBy(12.dp),

                        ) {

                            RowWithIcon("Appliance found on your network")

                            registeredIp?.let { ip ->

                                Text(ip, style = MaterialTheme.typography.headlineMedium)

                            }

                            registeredDevice?.name?.let { name ->

                                Text(name, style = MaterialTheme.typography.bodyLarge)

                            }

                        }

                    }

                    if (registeredDevice != null) {

                        Button(

                            onClick = { onOpenDashboard(registeredDevice) },

                            modifier = Modifier.fillMaxWidth(),

                        ) {

                            Text("Open fleet dashboard")

                        }

                    }

                }



                OnboardingPhase.Discovering -> {

                    Text(

                        "Setup complete",

                        style = MaterialTheme.typography.headlineSmall,

                        fontWeight = FontWeight.SemiBold,

                    )

                    Text(

                        "Scanning local network…",

                        style = MaterialTheme.typography.bodyLarge,

                    )

                    Column(

                        modifier = Modifier.fillMaxWidth(),

                        horizontalAlignment = Alignment.CenterHorizontally,

                        verticalArrangement = Arrangement.spacedBy(12.dp),

                    ) {

                        CircularProgressIndicator()

                        Text(discoveryProgress ?: "Looking for your appliance on the LAN…")

                    }

                }



                else -> {

                    Text(

                        "Setup complete",

                        style = MaterialTheme.typography.headlineSmall,

                        fontWeight = FontWeight.SemiBold,

                    )

                    Text(

                        "Reconnect your phone to",

                        style = MaterialTheme.typography.bodyLarge,

                    )

                    Text(

                        wifiLabel,

                        style = MaterialTheme.typography.titleLarge,

                        fontWeight = FontWeight.Medium,

                        color = MaterialTheme.colorScheme.primary,

                    )

                    Text(

                        "to finish registration.",

                        style = MaterialTheme.typography.bodyLarge,

                    )

                    errorMessage?.let {

                        Text(it, color = MaterialTheme.colorScheme.error)

                    }

                    Button(

                        onClick = {

                            onOpenWifiSettings()

                            context.startActivity(Intent(Settings.ACTION_WIFI_SETTINGS))

                        },

                        modifier = Modifier.fillMaxWidth(),

                    ) {

                        Text("Open Wi-Fi settings")

                    }

                    OutlinedButton(onClick = onRetryDiscovery, modifier = Modifier.fillMaxWidth()) {

                        Text("I'm back on Wi-Fi — find appliance")

                    }

                    OutlinedButton(onClick = onCancel, modifier = Modifier.fillMaxWidth()) {

                        Text("Cancel")

                    }

                }

            }

        }

    }

}



@Composable

private fun RowWithIcon(text: String) {

    androidx.compose.foundation.layout.Row(

        horizontalArrangement = Arrangement.spacedBy(8.dp),

        verticalAlignment = Alignment.CenterVertically,

    ) {

        Icon(

            Icons.Default.CheckCircle,

            contentDescription = null,

            tint = MaterialTheme.colorScheme.primary,

        )

        Text(text, style = MaterialTheme.typography.bodyLarge)

    }

}


