package com.micify.app

import android.Manifest
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.widget.Button
import android.widget.LinearLayout
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import com.google.android.material.switchmaterial.SwitchMaterial

private const val USB_PORT = 44551
private const val USB_HOST = "127.0.0.1"

class MainActivity : AppCompatActivity() {

    private lateinit var usbModeSwitch: SwitchMaterial
    private lateinit var wifiSection: View
    private lateinit var usbSection: View
    private lateinit var scanningLabel: TextView
    private lateinit var deviceListContainer: LinearLayout
    private lateinit var usbConnectButton: Button
    private lateinit var statusText: TextView
    private lateinit var stopButton: Button

    private lateinit var discovery: PcDiscovery
    private var pendingConnect: Triple<String, Int, String>? = null

    private val requestPermissions = registerForActivityResult(
        androidx.activity.result.contract.ActivityResultContracts.RequestMultiplePermissions()
    ) { results ->
        val target = pendingConnect
        pendingConnect = null
        val micGranted = results[Manifest.permission.RECORD_AUDIO] ?: hasMicPermission()
        if (micGranted && target != null) {
            doConnect(target.first, target.second, target.third)
        } else if (!micGranted) {
            statusText.text = getString(R.string.status_permission_needed)
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        usbModeSwitch = findViewById(R.id.usbModeSwitch)
        wifiSection = findViewById(R.id.wifiSection)
        usbSection = findViewById(R.id.usbSection)
        scanningLabel = findViewById(R.id.scanningLabel)
        deviceListContainer = findViewById(R.id.deviceListContainer)
        usbConnectButton = findViewById(R.id.usbConnectButton)
        statusText = findViewById(R.id.statusText)
        stopButton = findViewById(R.id.stopButton)

        discovery = PcDiscovery { devices -> onDevicesChanged(devices) }

        usbModeSwitch.setOnCheckedChangeListener { _, checked ->
            wifiSection.visibility = if (checked) View.GONE else View.VISIBLE
            usbSection.visibility = if (checked) View.VISIBLE else View.GONE
            if (checked) discovery.stop() else if (!MicifyService.isRunning) discovery.start()
        }

        usbConnectButton.setOnClickListener { connectRequestingPermissions(USB_HOST, USB_PORT, "USB") }

        stopButton.setOnClickListener { stopStreaming() }

        refreshUiForServiceState()
    }

    override fun onStart() {
        super.onStart()
        refreshUiForServiceState()
        if (!usbModeSwitch.isChecked && !MicifyService.isRunning) {
            discovery.start()
        }
    }

    override fun onStop() {
        discovery.stop()
        super.onStop()
    }

    private fun refreshUiForServiceState() {
        if (MicifyService.isRunning) {
            statusText.text = getString(R.string.status_streaming)
            stopButton.visibility = View.VISIBLE
            usbModeSwitch.isEnabled = false
        } else {
            statusText.text = getString(R.string.status_idle)
            stopButton.visibility = View.GONE
            usbModeSwitch.isEnabled = true
        }
    }

    private fun onDevicesChanged(devices: List<DiscoveredPc>) {
        scanningLabel.text = if (devices.isEmpty()) {
            getString(R.string.status_scanning)
        } else {
            "Found ${devices.size} PC(s):"
        }

        deviceListContainer.removeAllViews()
        for (pc in devices) {
            val row = LayoutInflater.from(this).inflate(R.layout.item_device, deviceListContainer, false)
            row.findViewById<TextView>(R.id.deviceLabel).text = "${pc.name} (${pc.host})"
            row.findViewById<Button>(R.id.connectButton).setOnClickListener {
                connectRequestingPermissions(pc.host, pc.port, pc.name)
            }
            deviceListContainer.addView(row)
        }
    }

    private fun hasMicPermission(): Boolean =
        ContextCompat.checkSelfPermission(this, Manifest.permission.RECORD_AUDIO) ==
            PackageManager.PERMISSION_GRANTED

    private fun hasNotificationPermission(): Boolean =
        Build.VERSION.SDK_INT < Build.VERSION_CODES.TIRAMISU ||
            ContextCompat.checkSelfPermission(this, Manifest.permission.POST_NOTIFICATIONS) ==
            PackageManager.PERMISSION_GRANTED

    private fun connectRequestingPermissions(host: String, port: Int, label: String) {
        val needed = mutableListOf<String>()
        if (!hasMicPermission()) needed.add(Manifest.permission.RECORD_AUDIO)
        if (!hasNotificationPermission()) needed.add(Manifest.permission.POST_NOTIFICATIONS)

        if (needed.isEmpty()) {
            doConnect(host, port, label)
        } else {
            pendingConnect = Triple(host, port, label)
            requestPermissions.launch(needed.toTypedArray())
        }
    }

    private fun doConnect(host: String, port: Int, label: String) {
        discovery.stop()
        val useTcp = usbModeSwitch.isChecked

        val intent = Intent(this, MicifyService::class.java).apply {
            action = MicifyService.ACTION_CONNECT
            putExtra(MicifyService.EXTRA_HOST, host)
            putExtra(MicifyService.EXTRA_PORT, port)
            putExtra(MicifyService.EXTRA_USE_TCP, useTcp)
            putExtra(MicifyService.EXTRA_LABEL, label)
        }
        ContextCompat.startForegroundService(this, intent)

        statusText.text = getString(R.string.status_streaming)
        stopButton.visibility = View.VISIBLE
        usbModeSwitch.isEnabled = false
    }

    private fun stopStreaming() {
        val intent = Intent(this, MicifyService::class.java).apply { action = MicifyService.ACTION_STOP }
        startService(intent)

        statusText.text = getString(R.string.status_idle)
        stopButton.visibility = View.GONE
        usbModeSwitch.isEnabled = true
        if (!usbModeSwitch.isChecked) discovery.start()
    }

    override fun onDestroy() {
        discovery.stop()
        super.onDestroy()
    }
}
