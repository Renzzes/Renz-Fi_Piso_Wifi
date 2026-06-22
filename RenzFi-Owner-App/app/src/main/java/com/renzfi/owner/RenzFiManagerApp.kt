package com.renzfi.owner

import android.app.Application
import android.webkit.WebView

class RenzFiManagerApp : Application() {
    override fun onCreate() {
        super.onCreate()
        WebView.setWebContentsDebuggingEnabled(BuildConfig.DEBUG)
    }
}
