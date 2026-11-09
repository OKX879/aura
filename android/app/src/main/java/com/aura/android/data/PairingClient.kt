package com.aura.android.data

import com.aura.android.data.model.ApiResponse
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import kotlinx.serialization.Serializable
import kotlinx.serialization.encodeToString
import kotlinx.serialization.json.Json
import okhttp3.MediaType.Companion.toMediaType
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.RequestBody.Companion.toRequestBody
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.Inet4Address
import java.net.InetAddress
import java.net.NetworkInterface
import java.net.SocketTimeoutException
import java.util.concurrent.TimeUnit

// 配对返回的设备信息
@Serializable
data class PairInfo(
    val serial: String = "",
    val model: String = "",
    val name: String = "",
    val endpoint: String = "",
    val firmwareVersion: String = ""
)

@Serializable
private data class PairVerifyRequest(val code: String)

@Serializable
private data class DiscoveryPayload(
    val proto: String = "",
    val serial: String = "",
    val name: String = "",
    val model: String = "",
    val endpoint: String = "",
    val port: Int = 3000
)

// 局域网扫描结果
data class DiscoveredDevice(
    val ip: String,
    val serial: String,
    val name: String,
    val model: String,
    val endpoint: String
)

// 设备配对与发现
object PairingClient {

    private const val DISCOVERY_PORT = 37020

    private val client = OkHttpClient.Builder()
        .connectTimeout(6, TimeUnit.SECONDS)
        .readTimeout(6, TimeUnit.SECONDS)
        .build()

    private val json = Json { ignoreUnknownKeys = true }

    suspend fun verify(endpoint: String, code: String): Result<PairInfo> =
        withContext(Dispatchers.IO) {
            runCatching {
                val base = endpoint.trim().trimEnd('/')
                require(base.startsWith("http://") || base.startsWith("https://")) {
                    "设备地址需以 http:// 或 https:// 开头"
                }
                val request = Request.Builder()
                    .url("$base/api/v1/pair/verify")
                    .post(
                        json.encodeToString(PairVerifyRequest(code.trim()))
                            .toRequestBody("application/json".toMediaType())
                    )
                    .build()
                client.newCall(request).execute().use { response ->
                    val text = response.body?.string().orEmpty()
                    val parsed = runCatching {
                        json.decodeFromString<ApiResponse<PairInfo>>(text)
                    }.getOrElse {
                        throw IllegalStateException("配对接口返回异常（HTTP ${response.code}）")
                    }
                    if (!parsed.success || parsed.data == null) {
                        throw IllegalStateException(
                            parsed.error?.message ?: "配对码不正确（HTTP ${response.code}）"
                        )
                    }
                    parsed.data
                }
            }
        }

    suspend fun discover(timeoutMs: Long = 2200): List<DiscoveredDevice> =
        withContext(Dispatchers.IO) {
            val found = linkedMapOf<String, DiscoveredDevice>()
            try {
                DatagramSocket().use { socket ->
                    socket.broadcast = true
                    socket.soTimeout = 250
                    val probe = "AURA_PROBE".toByteArray()
                    // 逐个网段探测
                    val prefixes = localIpv4List()
                        .mapNotNull { it.substringBeforeLast('.', "").takeIf(String::isNotEmpty) }
                        .distinct()
                        .take(3)
                    prefixes.forEach { prefix ->
                        for (host in 1..254) {
                            runCatching {
                                socket.send(
                                    DatagramPacket(
                                        probe,
                                        probe.size,
                                        InetAddress.getByName("$prefix.$host"),
                                        DISCOVERY_PORT
                                    )
                                )
                            }
                        }
                    }
                    runCatching {
                        socket.send(
                            DatagramPacket(
                                probe,
                                probe.size,
                                InetAddress.getByName("255.255.255.255"),
                                DISCOVERY_PORT
                            )
                        )
                    }
                    val deadline = System.currentTimeMillis() + timeoutMs
                    val buffer = ByteArray(2048)
                    while (System.currentTimeMillis() < deadline) {
                        val packet = DatagramPacket(buffer, buffer.size)
                        try {
                            socket.receive(packet)
                            val text = String(packet.data, 0, packet.length)
                            if (!text.contains("aura-discovery")) continue
                            val payload = json.decodeFromString<DiscoveryPayload>(text)
                            val ip = packet.address.hostAddress ?: continue
                            val endpoint = if (payload.endpoint.isNotBlank()) {
                                payload.endpoint
                            } else {
                                "http://$ip:${payload.port}"
                            }
                            found[payload.serial] = DiscoveredDevice(
                                ip = ip,
                                serial = payload.serial,
                                name = payload.name,
                                model = payload.model,
                                endpoint = endpoint
                            )
                        } catch (e: SocketTimeoutException) {
                            // 继续等到总超时
                        }
                    }
                }
            } catch (e: Exception) {
                // 失败时返回已发现结果
            }
            found.values.toList()
        }

    private fun localIpv4List(): List<String> = NetworkInterface.getNetworkInterfaces()
        .toList()
        .filter { it.isUp && !it.isLoopback }
        .flatMap { it.inetAddresses.toList() }
        .filterIsInstance<Inet4Address>()
        .mapNotNull { it.hostAddress }
        .filter { !it.startsWith("169.254.") }
}
