package com.micify.app

import android.Manifest
import android.content.pm.PackageManager
import android.os.Bundle
import android.widget.Button
import android.widget.EditText
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import androidx.core.app.ActivityCompat
import androidx.core.content.ContextCompat
import com.google.android.material.switchmaterial.SwitchMaterial

class MainActivity : AppCompatActivity() {

    private lateinit var hostInput: EditText
    private lateinit var portInput: EditText
    private lateinit var usbModeSwitch: SwitchMaterial
    private lateinit var startStopButton: Button
    private lateinit var statusText: TextView

    private var streaming = false

    private val requestMicPermission = registerForActivityResult(
        androidx.activity.result.contract.ActivityResultContracts.RequestPermission()
    ) { granted ->
        if (granted) {
            beginStreaming()
        } else {
            statusText.text = getString(R.string.status_permission_needed)
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        hostInput = findViewById(R.id.hostInput)
        portInput = findViewById(R.id.portInput)
        usbModeSwitch = findViewById(R.id.usbModeSwitch)
        startStopButton = findViewById(R.id.startStopButton)
        statusText = findViewById(R.id.statusText)

        startStopButton.setOnClickListener {
            if (streaming) {
                stopStreaming()
            } else {
                requestMicPermissionThenStart()
            }
        }
    }

    private fun requestMicPermissionThenStart() {
        val granted = ContextCompat.checkSelfPermission(this, Manifest.permission.RECORD_AUDIO) ==
            PackageManager.PERMISSION_GRANTED
        if (granted) {
            beginStreaming()
        } else {
            requestMicPermission.launch(Manifest.permission.RECORD_AUDIO)
        }
    }

    private fun beginStreaming() {
        val host = hostInput.text.toString().trim()
        val port = portInput.text.toString().toIntOrNull()
        if (host.isEmpty() || port == null) {
            statusText.text = "Enter a valid host and port"
            return
        }

        // In USB mode, the PC daemon is reached over `adb forward tcp:PORT
        // tcp:PORT`, so the phone connects to its own loopback address -
        // adb forwards that to the PC's listening socket over the USB link.
        val effectiveHost = if (usbModeSwitch.isChecked) "127.0.0.1" else host

        val ok = NativeStreamer.start(effectiveHost, port, usbModeSwitch.isChecked)
        if (ok) {
            streaming = true
            startStopButton.text = getString(R.string.button_stop)
            statusText.text = getString(R.string.status_streaming)
        } else {
            statusText.text = "Failed to start streaming"
        }
    }

    private fun stopStreaming() {
        NativeStreamer.stop()
        streaming = false
        startStopButton.text = getString(R.string.button_start)
        statusText.text = getString(R.string.status_idle)
    }

    override fun onDestroy() {
        if (streaming) {
            NativeStreamer.stop()
        }
        super.onDestroy()
    }
}
