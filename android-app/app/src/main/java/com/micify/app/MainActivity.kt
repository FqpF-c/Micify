package com.micify.app

import android.Manifest
import android.content.pm.PackageManager
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
    private var streaming = false
    private var pendingConnect: Pair<String, Int>? = null

    private val requestMicPermission = registerForActivityResult(
        androidx.activity.result.contract.ActivityResultContracts.RequestPermission()
    ) { granted ->
        val target = pendingConnect
        pendingConnect = null
        if (granted && target != null) {
            doConnect(target.first, target.second)
        } else if (!granted) {
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
            if (checked) discovery.stop() else discovery.start()
        }

        usbConnectButton.setOnClickListener { connectRequestingPermission(USB_HOST, USB_PORT) }

        stopButton.setOnClickListener { stopStreaming() }
    }

    override fun onStart() {
        super.onStart()
        if (!usbModeSwitch.isChecked && !streaming) {
            discovery.start()
        }
    }

    override fun onStop() {
        discovery.stop()
        super.onStop()
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
                connectRequestingPermission(pc.host, pc.port)
            }
            deviceListContainer.addView(row)
        }
    }

    private fun connectRequestingPermission(host: String, port: Int) {
        val granted = ContextCompat.checkSelfPermission(this, Manifest.permission.RECORD_AUDIO) ==
            PackageManager.PERMISSION_GRANTED
        if (granted) {
            doConnect(host, port)
        } else {
            pendingConnect = host to port
            requestMicPermission.launch(Manifest.permission.RECORD_AUDIO)
        }
    }

    private fun doConnect(host: String, port: Int) {
        discovery.stop()
        val useTcp = usbModeSwitch.isChecked
        val ok = NativeStreamer.start(host, port, useTcp)
        if (ok) {
            streaming = true
            statusText.text = getString(R.string.status_streaming)
            stopButton.visibility = View.VISIBLE
            usbModeSwitch.isEnabled = false
        } else {
            statusText.text = "Failed to connect to $host:$port"
            if (!useTcp) discovery.start()
        }
    }

    private fun stopStreaming() {
        NativeStreamer.stop()
        streaming = false
        statusText.text = getString(R.string.status_idle)
        stopButton.visibility = View.GONE
        usbModeSwitch.isEnabled = true
        if (!usbModeSwitch.isChecked) discovery.start()
    }

    override fun onDestroy() {
        if (streaming) {
            NativeStreamer.stop()
        }
        discovery.stop()
        super.onDestroy()
    }
}
