package com.micify.app

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.Service
import android.content.Context
import android.content.Intent
import android.os.Build
import android.os.IBinder
import androidx.core.app.NotificationCompat

/**
 * Owns the actual NativeStreamer session and runs as a foreground service
 * so mic capture survives the app leaving the foreground - e.g. switching
 * to Discord to test the mic, which is exactly what silently killed
 * capture before this existed (Android revokes background mic access
 * within seconds of an app losing foreground, and nothing told the app's
 * own UI that had happened).
 *
 * The persistent notification (chronometer-based, so the elapsed time
 * updates on its own with no per-second polling from us) doubles as the
 * "connected + duration" indicator, replacing the old static "Streaming..."
 * in-app text.
 */
class MicifyService : Service() {

    companion object {
        const val ACTION_CONNECT = "com.micify.app.action.CONNECT"
        const val ACTION_STOP = "com.micify.app.action.STOP"
        const val EXTRA_HOST = "host"
        const val EXTRA_PORT = "port"
        const val EXTRA_USE_TCP = "use_tcp"
        const val EXTRA_LABEL = "label"

        private const val CHANNEL_ID = "micify_streaming"
        private const val NOTIFICATION_ID = 1

        @Volatile
        var isRunning = false
            private set
    }

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        when (intent?.action) {
            ACTION_CONNECT -> {
                val host = intent.getStringExtra(EXTRA_HOST)
                val port = intent.getIntExtra(EXTRA_PORT, 44551)
                val useTcp = intent.getBooleanExtra(EXTRA_USE_TCP, false)
                val label = intent.getStringExtra(EXTRA_LABEL) ?: host ?: "PC"

                if (host == null) {
                    stopSelf(startId)
                    return START_NOT_STICKY
                }

                createChannel()
                startForeground(NOTIFICATION_ID, buildNotification(label))

                val ok = NativeStreamer.start(host, port, useTcp)
                isRunning = ok
                if (!ok) {
                    stopForeground(STOP_FOREGROUND_REMOVE)
                    stopSelf(startId)
                }
            }
            ACTION_STOP -> {
                if (isRunning) {
                    NativeStreamer.stop()
                    isRunning = false
                }
                stopForeground(STOP_FOREGROUND_REMOVE)
                stopSelf(startId)
            }
        }
        return START_NOT_STICKY
    }

    override fun onDestroy() {
        if (isRunning) {
            NativeStreamer.stop()
            isRunning = false
        }
        super.onDestroy()
    }

    private fun createChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val manager = getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
            if (manager.getNotificationChannel(CHANNEL_ID) == null) {
                val channel = NotificationChannel(
                    CHANNEL_ID, "Micify streaming", NotificationManager.IMPORTANCE_LOW
                )
                channel.description = "Shows when Micify is streaming your mic to a PC"
                manager.createNotificationChannel(channel)
            }
        }
    }

    private fun buildNotification(label: String): Notification {
        return NotificationCompat.Builder(this, CHANNEL_ID)
            .setContentTitle("Micify - connected")
            .setContentText("Streaming to $label")
            .setSmallIcon(R.drawable.ic_notification)
            .setUsesChronometer(true)
            .setOngoing(true)
            .setPriority(NotificationCompat.PRIORITY_LOW)
            .build()
    }
}
