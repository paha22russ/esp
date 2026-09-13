package ru.paha.kotellocal

import android.Manifest
import android.annotation.SuppressLint
import android.content.Context
import android.content.pm.PackageManager
import android.net.nsd.NsdManager
import android.net.nsd.NsdServiceInfo
import android.net.wifi.WifiManager
import android.os.Build
import android.os.Bundle
import android.view.KeyEvent
import android.webkit.WebChromeClient
import android.webkit.WebSettings
import android.webkit.WebView
import android.webkit.WebViewClient
import android.widget.Button
import android.widget.EditText
import android.widget.ProgressBar
import android.widget.TextView
import androidx.activity.ComponentActivity
import androidx.core.app.ActivityCompat
import androidx.core.content.ContextCompat
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.async
import kotlinx.coroutines.awaitAll
import kotlinx.coroutines.cancel
import kotlinx.coroutines.launch
import kotlinx.coroutines.supervisorScope
import kotlinx.coroutines.sync.Semaphore
import kotlinx.coroutines.sync.withPermit
import kotlinx.coroutines.withContext
import org.json.JSONObject
import java.net.HttpURLConnection
import java.net.InetAddress
import java.net.NetworkInterface
import java.net.URL
import java.util.Collections
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicReference

class MainActivity : ComponentActivity() {
    private lateinit var statusText: TextView
    private lateinit var webView: WebView
    private lateinit var progressBar: ProgressBar
    private lateinit var retryButton: Button
    private lateinit var refreshButton: Button
    private lateinit var manualUrlInput: EditText
    private lateinit var openManualButton: Button

    private val scope = CoroutineScope(Dispatchers.Main + Job())
    private val prefs by lazy { getSharedPreferences("kotel_prefs", MODE_PRIVATE) }
    private var discoveredBaseUrl: String? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        bindViews()
        setupWebView()
        setupButtons()
        maybeRequestNotificationPermission()
        startDiscovery()
    }

    override fun onDestroy() {
        super.onDestroy()
        scope.cancel()
    }

    override fun onKeyDown(keyCode: Int, event: KeyEvent?): Boolean {
        if (keyCode == KeyEvent.KEYCODE_BACK && webView.canGoBack()) {
            webView.goBack()
            return true
        }
        return super.onKeyDown(keyCode, event)
    }

    private fun bindViews() {
        statusText = findViewById(R.id.statusText)
        webView = findViewById(R.id.webView)
        progressBar = findViewById(R.id.progressBar)
        retryButton = findViewById(R.id.retryButton)
        refreshButton = findViewById(R.id.refreshButton)
        manualUrlInput = findViewById(R.id.manualUrlInput)
        openManualButton = findViewById(R.id.openManualButton)
    }

    @SuppressLint("SetJavaScriptEnabled")
    private fun setupWebView() {
        val settings = webView.settings
        settings.javaScriptEnabled = true
        settings.domStorageEnabled = true
        settings.cacheMode = WebSettings.LOAD_DEFAULT
        settings.loadsImagesAutomatically = true
        webView.webViewClient = WebViewClient()
        webView.webChromeClient = WebChromeClient()
    }

    private fun setupButtons() {
        retryButton.setOnClickListener { startDiscovery(forceRescan = true) }
        refreshButton.setOnClickListener { webView.reload() }
        openManualButton.setOnClickListener {
            val raw = manualUrlInput.text.toString().trim()
            val normalized = normalizeUrl(raw)
            if (normalized != null) {
                discoveredBaseUrl = normalized
                saveLastUrl(normalized)
                loadUrl(normalized)
            } else {
                statusText.text = "Неверный URL. Пример: http://192.168.31.103"
            }
        }
    }

    private fun startDiscovery(forceRescan: Boolean = false) {
        scope.launch {
            progressBar.isIndeterminate = true
            progressBar.visibility = ProgressBar.VISIBLE
            statusText.text = getString(R.string.searching)

            val result = withContext(Dispatchers.IO) { discoverEsp(forceRescan) }
            progressBar.visibility = ProgressBar.GONE

            if (result != null) {
                discoveredBaseUrl = result
                saveLastUrl(result)
                loadUrl(result)
            } else {
                statusText.text = getString(R.string.not_found)
            }
        }
    }

    private fun loadUrl(baseUrl: String) {
        statusText.text = getString(R.string.connected_to, baseUrl)
        webView.loadUrl(baseUrl)
    }

    private fun discoverEsp(forceRescan: Boolean): String? {
        if (!forceRescan) {
            val last = prefs.getString(KEY_LAST_URL, null)
            if (!last.isNullOrBlank() && isEspAt(last, timeoutMs = 1500)) return last
        }

        val kotelLocal = "http://kotel.local"
        if (isEspAt(kotelLocal, timeoutMs = 2000)) return kotelLocal

        val nsdFound = discoverViaNsd()
        if (!nsdFound.isNullOrBlank()) return nsdFound

        return scanSubnetForEsp()
    }

    private fun discoverViaNsd(): String? {
        val manager = getSystemService(Context.NSD_SERVICE) as? NsdManager ?: return null
        val wifiManager = applicationContext.getSystemService(Context.WIFI_SERVICE) as? WifiManager
        val lock = wifiManager?.createMulticastLock("kotel-nsd-lock")?.apply { setReferenceCounted(false) }
        lock?.acquire()
        return try {
            val found = AtomicReference<String?>(null)
            val done = AtomicBoolean(false)
            val latch = CountDownLatch(1)
            lateinit var discoveryListener: NsdManager.DiscoveryListener
            discoveryListener = object : NsdManager.DiscoveryListener {
                override fun onStartDiscoveryFailed(serviceType: String?, errorCode: Int) {
                    done.set(true)
                    latch.countDown()
                }

                override fun onStopDiscoveryFailed(serviceType: String?, errorCode: Int) {
                    done.set(true)
                    latch.countDown()
                }

                override fun onDiscoveryStarted(serviceType: String?) = Unit
                override fun onDiscoveryStopped(serviceType: String?) = Unit

                override fun onServiceFound(serviceInfo: NsdServiceInfo) {
                    if (done.get() || serviceInfo.serviceType != "_http._tcp.") return
                    manager.resolveService(
                        serviceInfo,
                        object : NsdManager.ResolveListener {
                            override fun onResolveFailed(serviceInfo: NsdServiceInfo?, errorCode: Int) = Unit

                            override fun onServiceResolved(resolved: NsdServiceInfo) {
                                if (done.get()) return
                                val hostAddress = resolved.host?.hostAddress ?: return
                                val url = "http://$hostAddress"
                                if (isEspAt(url, timeoutMs = 1500)) {
                                    found.set(url)
                                    done.set(true)
                                    latch.countDown()
                                }
                            }
                        }
                    )
                }

                override fun onServiceLost(serviceInfo: NsdServiceInfo?) = Unit
            }

            manager.discoverServices("_http._tcp.", NsdManager.PROTOCOL_DNS_SD, discoveryListener)
            latch.await(5, TimeUnit.SECONDS)
            runCatching { manager.stopServiceDiscovery(discoveryListener) }
            found.get()
        } finally {
            runCatching { lock?.release() }
        }
    }

    private fun scanSubnetForEsp(): String? {
        val subnetPrefix = getLocalSubnetPrefix() ?: return null
        return runCatching {
            kotlinx.coroutines.runBlocking {
                supervisorScope {
                    val semaphore = Semaphore(24)
                    (1..254).map { host ->
                        async {
                            semaphore.withPermit {
                                val candidate = "http://$subnetPrefix.$host"
                                if (isEspAt(candidate, timeoutMs = 1200)) candidate else null
                            }
                        }
                    }.awaitAll().firstOrNull { it != null }
                }
            }
        }.getOrNull()
    }

    private fun getLocalSubnetPrefix(): String? {
        val wifiManager = applicationContext.getSystemService(Context.WIFI_SERVICE) as? WifiManager
        val ip = wifiManager?.connectionInfo?.ipAddress ?: return getInterfaceSubnetFallback()
        if (ip == 0) return getInterfaceSubnetFallback()
        val b1 = ip and 0xFF
        val b2 = ip shr 8 and 0xFF
        val b3 = ip shr 16 and 0xFF
        return "$b1.$b2.$b3"
    }

    private fun getInterfaceSubnetFallback(): String? {
        return runCatching {
            val interfaces = Collections.list(NetworkInterface.getNetworkInterfaces())
            interfaces.asSequence()
                .filter { it.isUp && !it.isLoopback }
                .flatMap { Collections.list(it.inetAddresses).asSequence() }
                .mapNotNull { address ->
                    if (address is InetAddress && address.hostAddress?.contains(':') == false) {
                        address.hostAddress?.split('.')?.takeIf { it.size == 4 }?.take(3)?.joinToString(".")
                    } else null
                }
                .firstOrNull()
        }.getOrNull()
    }

    private fun isEspAt(baseUrl: String, timeoutMs: Int): Boolean {
        val statusUrl = "$baseUrl/api/status"
        val body = httpGet(statusUrl, timeoutMs) ?: return false
        return runCatching {
            val json = JSONObject(body)
            json.has("firmwareVersion") && json.has("state") && json.has("supplyTemp")
        }.getOrElse {
            val root = httpGet(baseUrl, timeoutMs) ?: return false
            root.contains("ESP32 Boiler Control", ignoreCase = true) ||
                root.contains("Котел", ignoreCase = true)
        }
    }

    private fun httpGet(url: String, timeoutMs: Int): String? {
        return runCatching {
            val connection = (URL(url).openConnection() as HttpURLConnection).apply {
                requestMethod = "GET"
                connectTimeout = timeoutMs
                readTimeout = timeoutMs
                setRequestProperty("Connection", "close")
            }
            connection.inputStream.bufferedReader().use { it.readText() }.also {
                connection.disconnect()
            }
        }.getOrNull()
    }

    private fun normalizeUrl(raw: String): String? {
        if (raw.isBlank()) return null
        val withScheme = if (raw.startsWith("http://") || raw.startsWith("https://")) raw else "http://$raw"
        return runCatching { URL(withScheme); withScheme.trimEnd('/') }.getOrNull()
    }

    private fun saveLastUrl(url: String) {
        prefs.edit().putString(KEY_LAST_URL, url).apply()
    }

    private fun maybeRequestNotificationPermission() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.TIRAMISU) return
        val granted = ContextCompat.checkSelfPermission(this, Manifest.permission.POST_NOTIFICATIONS) == PackageManager.PERMISSION_GRANTED
        if (!granted) {
            ActivityCompat.requestPermissions(this, arrayOf(Manifest.permission.POST_NOTIFICATIONS), REQ_NOTIFICATIONS)
        }
    }

    companion object {
        private const val KEY_LAST_URL = "last_url"
        private const val REQ_NOTIFICATIONS = 101
    }
}
