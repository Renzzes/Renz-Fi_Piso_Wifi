package com.renzfi.owner.webview

import android.annotation.SuppressLint
import android.graphics.Bitmap
import android.view.ViewGroup
import android.webkit.CookieManager
import android.webkit.WebChromeClient
import android.webkit.WebResourceError
import android.webkit.WebResourceRequest
import android.webkit.WebSettings
import android.webkit.WebView
import android.webkit.WebViewClient
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.viewinterop.AndroidView

@SuppressLint("SetJavaScriptEnabled")
@Composable
fun RenzFiWebView(
    url: String,
    modifier: Modifier = Modifier,
    onWebViewReady: (WebView) -> Unit = {},
    onCanGoBackChanged: (Boolean) -> Unit = {},
) {
    var isLoading by remember(url) { mutableStateOf(true) }
    var progress by remember(url) { mutableFloatStateOf(0f) }
    var isOffline by remember(url) { mutableStateOf(false) }
    var offlineMessage by remember(url) { mutableStateOf<String?>(null) }
    var webViewRef by remember { mutableStateOf<WebView?>(null) }

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
                            databaseEnabled = true
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
                        }

                        webViewClient = object : WebViewClient() {
                            override fun shouldOverrideUrlLoading(
                                view: WebView?,
                                request: WebResourceRequest?,
                            ): Boolean {
                                val requestUrl = request?.url?.toString() ?: return false
                                if (requestUrl.startsWith("http://") || requestUrl.startsWith("https://")) {
                                    view?.loadUrl(requestUrl)
                                    return true
                                }
                                return false
                            }

                            override fun onPageStarted(view: WebView?, url: String?, favicon: Bitmap?) {
                                isOffline = false
                                offlineMessage = null
                                isLoading = true
                            }

                            override fun onPageFinished(view: WebView?, url: String?) {
                                isLoading = false
                                progress = 1f
                                onCanGoBackChanged(view?.canGoBack() == true)
                            }

                            override fun onReceivedError(
                                view: WebView?,
                                request: WebResourceRequest?,
                                error: WebResourceError?,
                            ) {
                                if (request?.isForMainFrame == true) {
                                    isOffline = true
                                    offlineMessage =
                                        "Dashboard disconnected. Check your connection to the appliance."
                                    isLoading = false
                                }
                            }
                        }

                        loadUrl(url)
                        webViewRef = this
                        onWebViewReady(this)
                    }
                },
                update = { webView ->
                    if (webView.url != url) {
                        webView.loadUrl(url)
                    }
                    webViewRef = webView
                    onWebViewReady(webView)
                },
                modifier = Modifier.fillMaxSize(),
            )

            if (isOffline) {
                Column(
                    modifier = Modifier
                        .fillMaxSize()
                        .align(Alignment.Center),
                    horizontalAlignment = Alignment.CenterHorizontally,
                ) {
                    Text(
                        text = offlineMessage ?: "Dashboard offline",
                        style = MaterialTheme.typography.bodyLarge,
                        color = MaterialTheme.colorScheme.error,
                    )
                }
            }
        }
    }
}
