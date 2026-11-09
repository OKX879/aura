package com.aura.android.data.ai

import android.util.Log
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import okhttp3.OkHttpClient
import okhttp3.Request
import java.net.URLEncoder
import java.util.concurrent.TimeUnit

// 网页搜索结果
data class SearchResult(
    val title: String,
    val url: String,
    val snippet: String
)

// 无 Key 网页搜索
object WebSearch {

    private const val TAG = "AuraWebSearch"

    private const val USER_AGENT =
        "Mozilla/5.0 (Linux; Android 14; Pixel 8) AppleWebKit/537.36 " +
            "(KHTML, like Gecko) Chrome/126.0 Mobile Safari/537.36"

    private val http = OkHttpClient.Builder()
        .connectTimeout(12, TimeUnit.SECONDS)
        .readTimeout(20, TimeUnit.SECONDS)
        .followRedirects(true)
        .build()

    suspend fun search(query: String, limit: Int = 4): List<SearchResult> =
        withContext(Dispatchers.IO) {
            if (query.isBlank()) return@withContext emptyList()
            val encoded = URLEncoder.encode(query, "UTF-8")

            // 优先解析 HTML，失败退回 RSS
            val html = fetch("https://cn.bing.com/search?q=$encoded&setlang=zh-CN")
            parseHtml(html, limit).takeIf { it.isNotEmpty() }?.let { return@withContext it }

            val rss = fetch("https://cn.bing.com/search?q=$encoded&format=rss&setlang=zh-CN")
            parseRss(rss, limit)
        }

    private fun fetch(url: String): String {
        return runCatching {
            val request = Request.Builder()
                .url(url)
                .addHeader("User-Agent", USER_AGENT)
                .addHeader("Accept-Language", "zh-CN,zh;q=0.9")
                .get()
                .build()
            http.newCall(request).execute().use { response ->
                val body = response.body?.string().orEmpty()
                Log.d(TAG, "GET ${response.request.url} -> ${response.code} len=${body.length}")
                if (response.isSuccessful) body else ""
            }
        }.getOrElse {
            Log.w(TAG, "search failed: ${it.message}")
            ""
        }
    }

    fun parseRss(xml: String, limit: Int = 4): List<SearchResult> {
        if (xml.isBlank()) return emptyList()
        return Regex("<item>(.*?)</item>", RegexOption.DOT_MATCHES_ALL)
            .findAll(xml)
            .mapNotNull { match ->
                val block = match.groupValues[1]
                val title = tag(block, "title")
                val link = tag(block, "link")
                val snippet = tag(block, "description")
                if (title.isBlank() || !link.startsWith("http")) {
                    null
                } else {
                    SearchResult(title = title, url = link, snippet = snippet)
                }
            }
            .take(limit)
            .toList()
            .also { Log.d(TAG, "rss parsed ${it.size} items") }
    }

    fun parseHtml(html: String, limit: Int = 4): List<SearchResult> {
        if (html.isBlank()) return emptyList()
        val results = mutableListOf<SearchResult>()
        // 放宽结果块匹配
        val blocks = Regex(
            "<li[^>]*class=\"[^\"]*\\bb_algo\\b[^\"]*\"[^>]*>(.*?)</li>",
            RegexOption.DOT_MATCHES_ALL
        ).findAll(html).toList()
        Log.d(TAG, "html blocks=${blocks.size}")
        for (block in blocks) {
            if (results.size >= limit) break
            val value = block.value
            // 取最长锚文本作为标题
            val best = Regex(
                "<a[^>]*href=\"([^\"]+)\"[^>]*>(.*?)</a>",
                RegexOption.DOT_MATCHES_ALL
            ).findAll(value).mapNotNull { anchor ->
                val text = unescape(anchor.groupValues[2]).trim()
                val link = resolveUrl(anchor.groupValues[1])
                if (text.length >= 6 && link.startsWith("http")) text to link else null
            }.maxByOrNull { it.first.length } ?: continue
            val title = best.first
            val url = best.second
            val snippet = Regex("<p[^>]*>(.*?)</p>", RegexOption.DOT_MATCHES_ALL)
                .find(value)?.groupValues?.get(1)
                ?.let { unescape(it) }
                ?.replace(Regex("\\s+"), " ")
                ?.trim()
                .orEmpty()
            if (title.isNotEmpty() && url.startsWith("http")) {
                results.add(SearchResult(title = title, url = url, snippet = snippet))
            }
        }
        Log.d(TAG, "html parsed ${results.size} items")
        return results
    }

    // 解析跳转链接
    private fun resolveUrl(raw: String): String {
        if (!raw.contains("bing.com/ck/a")) return raw
        val encoded = Regex("[?&]u=a1([^&]+)").find(raw)?.groupValues?.get(1) ?: return raw
        return runCatching {
            val normalized = encoded.replace('-', '+').replace('_', '/')
            val padded = normalized + "=".repeat((4 - normalized.length % 4) % 4)
            val decoded = String(
                android.util.Base64.decode(padded, android.util.Base64.NO_WRAP),
                Charsets.UTF_8
            )
            if (decoded.startsWith("http")) decoded else raw
        }.getOrDefault(raw)
    }

    private fun tag(block: String, name: String): String =
        Regex("<$name>(.*?)</$name>", RegexOption.DOT_MATCHES_ALL)
            .find(block)?.groupValues?.get(1)
            ?.let { unescape(it) }
            ?.replace(Regex("\\s+"), " ")
            ?.trim()
            .orEmpty()

    private fun unescape(raw: String): String = raw
        .replace("<![CDATA[", "")
        .replace("]]>", "")
        .replace("&ensp;", " ")
        .replace("&nbsp;", " ")
        .replace("&#0183;", "·")
        .replace("&amp;", "&")
        .replace("&quot;", "\"")
        .replace("&#39;", "'")
        .replace("&lt;", "<")
        .replace("&gt;", ">")
        .replace(Regex("<[^>]+>"), "")
}
