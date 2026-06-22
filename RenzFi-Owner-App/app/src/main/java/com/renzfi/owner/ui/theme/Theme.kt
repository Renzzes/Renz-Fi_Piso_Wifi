package com.renzfi.owner.ui.theme

import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable

private val LightColorScheme = lightColorScheme(
    primary = RenzFiNavy,
    onPrimary = RenzFiOnPrimary,
    secondary = RenzFiSky,
    onSecondary = RenzFiNavy,
    background = RenzFiBackground,
    onBackground = RenzFiNavy,
    surface = RenzFiSurface,
    onSurface = RenzFiNavy,
    error = RenzFiError,
)

private val DarkColorScheme = darkColorScheme(
    primary = RenzFiSky,
    onPrimary = RenzFiNavy,
    secondary = RenzFiSky,
    onSecondary = RenzFiNavy,
    background = RenzFiNavy,
    onBackground = RenzFiOnPrimary,
    surface = RenzFiSlate,
    onSurface = RenzFiOnPrimary,
    error = RenzFiError,
)

@Composable
fun RenzFiManagerTheme(
    darkTheme: Boolean = isSystemInDarkTheme(),
    content: @Composable () -> Unit,
) {
    val colorScheme = if (darkTheme) DarkColorScheme else LightColorScheme

    MaterialTheme(
        colorScheme = colorScheme,
        typography = Typography,
        content = content,
    )
}
