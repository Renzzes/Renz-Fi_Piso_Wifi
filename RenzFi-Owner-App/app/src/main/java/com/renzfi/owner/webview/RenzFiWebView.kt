package com.renzfi.owner.webview

import android.annotation.SuppressLint
import android.graphics.Bitmap
import android.view.ViewGroup
import android.webkit.CookieManager
import android.webkit.WebChromeClient
import android.webkit.WebResourceError
import android.webkit.WebResourceRequest
import android.webkit.WebResourceResponse
import android.webkit.WebSettings
import android.webkit.WebView
import android.webkit.WebViewClient
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.viewinterop.AndroidView

/**
 * Hosts the appliance Admin Dashboard WebView. Network-level failures on the
 * main frame (connection aborted/refused, timeout, host lookup failure,
 * unavailable network, etc.) are reported via [onError] instead of letting
 * Chromium render its own raw error page — the caller (DashboardScreen) is
 * responsible for replacing this composable with a native offline state.
 *
 * The WebView is never constructed when [url] is blank or malformed.
 */
@SuppressLint("SetJavaScriptEnabled")
@Composable
fun RenzFiWebView(
    url: String,
    modifier: Modifier = Modifier,
    onWebViewReady: (WebView) -> Unit = {},
    onCanGoBackChanged: (Boolean) -> Unit = {},
    onError: () -> Unit = {},
) {
    val normalizedUrl = url.trim()
    val urlValid = remember(normalizedUrl) {
        normalizedUrl.startsWith("http://") || normalizedUrl.startsWith("https://")
    }
    val latestOnError by rememberUpdatedState(onError)
    val latestOnWebViewReady by rememberUpdatedState(onWebViewReady)
    val latestOnCanGoBackChanged by rememberUpdatedState(onCanGoBackChanged)

    if (!urlValid) {
        LaunchedEffect(normalizedUrl) {
            latestOnError()
        }
        return
    }

    var isLoading by remember(normalizedUrl) { mutableStateOf(true) }
    var progress by remember(normalizedUrl) { mutableFloatStateOf(0f) }

    Column(modifier = modifier.fillMaxSize()) {
        if (isLoading && progress < 1f) {
            LinearProgressIndicator(
                progress = { progress },
                modifier = Modifier.fillMaxWidth(),
                color = MaterialTheme.colorScheme.secondary,
            )
        }

        Box(modifier = Modifier.fillMaxSize()) {
            AndroidView(
                factory = { context ->
                    try {
                        WebView(context).apply {
                            layoutParams = ViewGroup.LayoutParams(
                                ViewGroup.LayoutParams.MATCH_PARENT,
                                ViewGroup.LayoutParams.MATCH_PARENT,
                            )

                            CookieManager.getInstance().setAcceptCookie(true)
                            CookieManager.getInstance().setAcceptThirdPartyCookies(this, true)

                            settings.apply {
                                javaScriptEnabled = true
                                domStorageEnabled = true
                                allowFileAccess = true
                                allowContentAccess = true
                                cacheMode = WebSettings.LOAD_DEFAULT
                                mixedContentMode = WebSettings.MIXED_CONTENT_COMPATIBILITY_MODE
                                userAgentString = "$userAgentString RenzFiManager/${com.renzfi.owner.BuildConfig.VERSION_NAME}"
                            }

                            webChromeClient = object : WebChromeClient() {
                                override fun onProgressChanged(view: WebView?, newProgress: Int) {
                                    progress = newProgress / 100f
                                    isLoading = newProgress < 100
                                }

                                override fun onConsoleMessage(message: android.webkit.ConsoleMessage?): Boolean {
                                    message?.let {
                                        android.util.Log.println(
                                            when (it.messageLevel()) {
                                                android.webkit.ConsoleMessage.MessageLevel.ERROR ->
                                                    android.util.Log.ERROR
                                                android.webkit.ConsoleMessage.MessageLevel.WARNING ->
                                                    android.util.Log.WARN
                                                else -> android.util.Log.DEBUG
                                            },
                                            "RenzFiWebView",
                                            "${it.message()} (${it.sourceId()}:${it.lineNumber()})",
                                        )
                                    }
                                    return super.onConsoleMessage(message)
                                }
                            }

                            webViewClient = object : WebViewClient() {
                                override fun shouldOverrideUrlLoading(
                                    view: WebView?,
                                    request: WebResourceRequest?,
                                ): Boolean {
                                    val requestUrl = request?.url?.toString() ?: return false
                                    if (requestUrl.startsWith("http://") || requestUrl.startsWith("https://")) {
                                        try {
                                            view?.loadUrl(requestUrl)
                                        } catch (_: Exception) {
                                            latestOnError()
                                        }
                                        return true
                                    }
                                    return false
                                }

                                override fun onPageStarted(view: WebView?, url: String?, favicon: Bitmap?) {
                                    isLoading = true
                                }

                                override fun onPageFinished(view: WebView?, url: String?) {
                                    isLoading = false
                                    progress = 1f
                                    latestOnCanGoBackChanged(view?.canGoBack() == true)
                                }

                                override fun onReceivedError(
                                    view: WebView?,
                                    request: WebResourceRequest?,
                                    error: WebResourceError?,
                                ) {
                                    if (request?.isForMainFrame == true) {
                                        isLoading = false
                                        latestOnError()
                                    }
                                }

                                override fun onReceivedHttpError(
                                    view: WebView?,
                                    request: WebResourceRequest?,
                                    errorResponse: WebResourceResponse?,
                                ) {
                                    if (request?.isForMainFrame == true) {
                                        isLoading = false
                                        latestOnError()
                                    }
                                }
                            }

                            loadUrl(normalizedUrl)
                            latestOnWebViewReady(this)
                        }
                    } catch (_: Exception) {
                        latestOnError()
                        WebView(context).apply {
                            layoutParams = ViewGroup.LayoutParams(
                                ViewGroup.LayoutParams.MATCH_PARENT,
                                ViewGroup.LayoutParams.MATCH_PARENT,
                            )
                        }
                    }
                },
                update = { webView ->
                    try {
                        if (webView.url != normalizedUrl) {
                            webView.loadUrl(normalizedUrl)
                        }
                        latestOnWebViewReady(webView)
                    } catch (_: Exception) {
                        latestOnError()
                    }
                },
                onRelease = { webView ->
                    try {
                        webView.stopLoading()
                        webView.webChromeClient = null
                        webView.destroy()
                    } catch (_: Exception) {
                        // Composable may already be gone — ignore teardown failures.
                    }
                },
                modifier = Modifier.fillMaxSize(),
            )
        }
    }
}
