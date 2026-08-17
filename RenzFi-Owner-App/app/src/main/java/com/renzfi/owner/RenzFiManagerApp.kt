package com.renzfi.owner

import android.app.Application
import android.webkit.WebView
import com.renzfi.owner.update.UpdateManager
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.launch

class RenzFiManagerApp : Application() {

    private val applicationScope = CoroutineScope(SupervisorJob() + Dispatchers.Default)

    /**
     * Application-scoped singleton. Exposed so ViewModels can access it
     * without a dependency injection framework.
     */
    val updateManager: UpdateManager by lazy { UpdateManager(this) }

    override fun onCreate() {
        super.onCreate()
        WebView.setWebContentsDebuggingEnabled(BuildConfig.DEBUG)

        // Kick off a background update check after the user has an internet
        // connection. The manager waits for network then adds a short delay
        // so startup LAN health checks are not impacted.
        applicationScope.launch {
            updateManager.scheduleStartupCheck()
        }
    }
}
