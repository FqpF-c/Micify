package com.micify.app

import android.os.Handler
import android.os.Looper
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.InetSocketAddress

data class DiscoveredPc(val name: String, val host: String, val port: Int) {
    val key: String get() = host // one entry per IP, latest beacon wins
}

/**
 * Listens for the PC daemon's discovery beacon (see docs/PROTOCOL.md,
 * "Discovery beacon") on UDP port 44552, so the phone never needs a
 * manually-typed IP address. Pure Kotlin/DatagramSocket - this is small,
 * infrequent traffic, unlike the audio path which stays in native code.
 */
class PcDiscovery(private val onDevicesChanged: (List<DiscoveredPc>) -> Unit) {

    companion object {
        private const val DISCOVERY_PORT = 44552
        private const val BEACON_SIZE = 40
        private const val MAGIC = 0x4449434D // "MICD", little-endian on the wire
        private const val STALE_TIMEOUT_MS = 5000L
    }

    private val handler = Handler(Looper.getMainLooper())
    private val devices = LinkedHashMap<String, Pair<DiscoveredPc, Long>>()
    private var socket: DatagramSocket? = null
    private var listenerThread: Thread? = null
    @Volatile private var running = false

    fun start() {
        if (running) return
        running = true

        listenerThread = Thread {
            try {
                val sock = DatagramSocket(null)
                sock.reuseAddress = true
                sock.bind(InetSocketAddress(DISCOVERY_PORT))
                sock.soTimeout = 1000
                socket = sock

                val buf = ByteArray(BEACON_SIZE)
                while (running) {
                    try {
                        val packet = DatagramPacket(buf, buf.size)
                        sock.receive(packet)
                        parseBeacon(packet)?.let { pc ->
                            devices[pc.key] = pc to System.currentTimeMillis()
                            publish()
                        }
                    } catch (_: java.net.SocketTimeoutException) {
                        // just a wakeup to check `running` and prune stale entries
                    }
                    pruneStale()
                }
            } catch (_: Exception) {
                // socket closed on stop(), or bind failed - either way, stop quietly
            }
        }
        listenerThread?.isDaemon = true
        listenerThread?.start()
    }

    fun stop() {
        running = false
        socket?.close()
        socket = null
        listenerThread = null
        devices.clear()
    }

    private fun parseBeacon(packet: DatagramPacket): DiscoveredPc? {
        if (packet.length != BEACON_SIZE) return null
        val data = packet.data

        val magic = readIntLE(data, 0)
        if (magic != MAGIC) return null

        val port = readShortLE(data, 6) and 0xFFFF
        val nameBytes = data.copyOfRange(8, 40)
        val nameEnd = nameBytes.indexOf(0).let { if (it < 0) nameBytes.size else it }
        val name = String(nameBytes, 0, nameEnd, Charsets.UTF_8).ifBlank { packet.address.hostAddress ?: "PC" }

        return DiscoveredPc(name = name, host = packet.address.hostAddress ?: return null, port = port)
    }

    private fun readIntLE(b: ByteArray, off: Int): Int =
        (b[off].toInt() and 0xFF) or
            ((b[off + 1].toInt() and 0xFF) shl 8) or
            ((b[off + 2].toInt() and 0xFF) shl 16) or
            ((b[off + 3].toInt() and 0xFF) shl 24)

    private fun readShortLE(b: ByteArray, off: Int): Int =
        (b[off].toInt() and 0xFF) or ((b[off + 1].toInt() and 0xFF) shl 8)

    private fun pruneStale() {
        val now = System.currentTimeMillis()
        val before = devices.size
        devices.entries.removeAll { now - it.value.second > STALE_TIMEOUT_MS }
        if (devices.size != before) publish()
    }

    private fun publish() {
        val snapshot = devices.values.map { it.first }
        handler.post { onDevicesChanged(snapshot) }
    }
}
