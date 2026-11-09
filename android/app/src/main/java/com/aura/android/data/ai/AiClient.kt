package com.aura.android.data.ai

import com.aura.android.data.model.ChatMessage
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.JsonPrimitive
import kotlinx.serialization.json.add
import kotlinx.serialization.json.buildJsonObject
import kotlinx.serialization.json.contentOrNull
import kotlinx.serialization.json.jsonArray
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.jsonPrimitive
import kotlinx.serialization.json.put
import kotlinx.serialization.json.putJsonArray
import kotlinx.serialization.json.putJsonObject
import okhttp3.MediaType.Companion.toMediaType
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.RequestBody.Companion.toRequestBody
import java.util.concurrent.TimeUnit

// AI 接口配置
data class AiConfig(
    val baseUrl: String = DEFAULT_BASE_URL,
    val apiKey: String = "",
    val model: String = DEFAULT_MODEL
) {
    // 本地推理服务判断
    private val isLocalEndpoint: Boolean
        get() = listOf("localhost", "127.0.0.1", "10.0.2.2", "192.168.", "10.")
            .any { baseUrl.contains(it) }

    val isConfigured: Boolean
        get() = baseUrl.isNotBlank() && model.isNotBlank() && (apiKey.isNotBlank() || isLocalEndpoint)

    val maskedKey: String
        get() = when {
            apiKey.isBlank() -> "未填写"
            apiKey.length <= 8 -> "已填写"
            else -> "${apiKey.take(5)}••••${apiKey.takeLast(4)}"
        }

    fun normalized(): AiConfig = copy(
        baseUrl = baseUrl.trim().trimEnd('/'),
        apiKey = apiKey.trim(),
        model = model.trim()
    )

    companion object {
        const val DEFAULT_BASE_URL = "https://api.deepseek.com/v1"
        const val DEFAULT_MODEL = "deepseek-chat"

        val PRESETS = listOf(
            AiPreset("DeepSeek", "https://api.deepseek.com/v1", "deepseek-flash"),
            AiPreset("OpenAI", "https://api.openai.com/v1", "gpt-4o-mini"),
            AiPreset("通义千问", "https://dashscope.aliyuncs.com/compatible-mode/v1", "qwen-plus"),
            AiPreset("本地 Ollama", "http://10.0.2.2:11434/v1", "qwen2.5:7b")
        )
    }
}

data class AiPreset(val label: String, val baseUrl: String, val model: String)

// 模型回复与来源
data class AiReply(
    val content: String,
    val sources: List<SearchResult> = emptyList()
)

// OpenAI 兼容接口客户端
object AiClient {

    private const val MAX_TOOL_ROUNDS = 2

    private val json = Json { ignoreUnknownKeys = true; isLenient = true }

    private val http = OkHttpClient.Builder()
        .connectTimeout(15, TimeUnit.SECONDS)
        .readTimeout(120, TimeUnit.SECONDS)
        .writeTimeout(30, TimeUnit.SECONDS)
        .build()

    suspend fun chat(
        config: AiConfig,
        messages: List<ChatMessage>,
        allowWebSearch: Boolean = false
    ): AiReply = withContext(Dispatchers.IO) {
        val cfg = config.normalized()
        require(cfg.baseUrl.isNotBlank()) { "接口地址为空" }
        require(cfg.model.isNotBlank()) { "模型名称为空" }

        val history = messages.map { msg ->
            buildJsonObject {
                put("role", msg.role)
                put("content", msg.content)
            }
        }.toMutableList()
        val sources = mutableListOf<SearchResult>()

        var round = 0
        var finalRoundNotified = false
        while (true) {
            val toolsAvailable = allowWebSearch && round < MAX_TOOL_ROUNDS
            // 最后一轮提示只输出正文
            if (!toolsAvailable && !finalRoundNotified) {
                finalRoundNotified = true
                history.add(
                    buildJsonObject {
                        put("role", "system")
                        put("content", "检索阶段已结束。现在只输出面向用户的最终回答，不要再调用或模拟任何工具，也不要输出 DSML/XML 形式的调用标记。")
                    }
                )
            }
            val payload = buildJsonObject {
                put("model", cfg.model)
                put("temperature", 0.6)
                put("stream", false)
                putJsonArray("messages") { history.forEach { add(it) } }
                if (toolsAvailable) {
                    putJsonArray("tools") { add(webSearchTool()) }
                }
            }.toString()

            val message = post(cfg, payload)
            val toolCalls = message["tool_calls"]?.jsonArray
            if (toolCalls.isNullOrEmpty() || !toolsAvailable) {
                val content = message["content"]?.jsonPrimitive?.contentOrNull
                    ?.let { sanitize(it) }
                    ?.takeIf { it.isNotEmpty() }
                    ?: throw IllegalStateException("模型返回了空内容")
                return@withContext AiReply(
                    content = content,
                    sources = sources.distinctBy { it.url }
                )
            }

            // 只回传工具调用
            history.add(
                buildJsonObject {
                    put("role", "assistant")
                    put("content", "")
                    put("tool_calls", toolCalls)
                }
            )
            for (call in toolCalls) {
                val obj = call.jsonObject
                val id = obj["id"]?.jsonPrimitive?.contentOrNull ?: continue
                val name = obj["function"]?.jsonObject?.get("name")?.jsonPrimitive?.contentOrNull
                val arguments = obj["function"]?.jsonObject?.get("arguments")?.jsonPrimitive?.contentOrNull
                val query = runCatching {
                    json.parseToJsonElement(arguments.orEmpty())
                        .jsonObject["query"]?.jsonPrimitive?.contentOrNull
                }.getOrNull()?.takeIf { it.isNotBlank() }

                val results = if (name == "web_search" && query != null) {
                    runCatching { WebSearch.search(query) }.getOrDefault(emptyList())
                } else {
                    emptyList()
                }
                android.util.Log.d(
                    "AuraAI",
                    "tool=$name query=$query results=${results.size}"
                )
                sources += results

                val text = if (results.isEmpty()) {
                    "没有检索到结果，请基于已有知识回答，并说明未能联网核实。"
                } else {
                    results.joinToString("\n\n") {
                        "标题：${it.title}\n摘要：${it.snippet}\n链接：${it.url}"
                    }
                }
                history.add(
                    buildJsonObject {
                        put("role", "tool")
                        put("tool_call_id", id)
                        put("content", text)
                    }
                )
            }
            round++
        }
        @Suppress("UNREACHABLE_CODE")
        AiReply("")
    }

    private fun webSearchTool() = buildJsonObject {
        put("type", "function")
        putJsonObject("function") {
            put("name", "web_search")
            put("description", "联网搜索。用于新闻、实时数据、价格、政策、赛事、人物近况等模型知识之外的信息；一次只搜一个查询词。")
            putJsonObject("parameters") {
                put("type", "object")
                putJsonObject("properties") {
                    putJsonObject("query") {
                        put("type", "string")
                        put("description", "搜索关键词，尽量具体")
                    }
                }
                putJsonArray("required") { add(JsonPrimitive("query")) }
            }
        }
    }

    private fun post(cfg: AiConfig, payload: String): JsonObject {
        val builder = Request.Builder()
            .url("${cfg.baseUrl}/chat/completions")
            .addHeader("Content-Type", "application/json")
            .post(payload.toRequestBody("application/json".toMediaType()))
        if (cfg.apiKey.isNotBlank()) {
            builder.addHeader("Authorization", "Bearer ${cfg.apiKey}")
        }
        http.newCall(builder.build()).execute().use { response ->
            val raw = response.body?.string().orEmpty()
            if (!response.isSuccessful) {
                throw IllegalStateException(extractError(response.code, raw))
            }
            return json.parseToJsonElement(raw).jsonObject["choices"]?.jsonArray
                ?.firstOrNull()?.jsonObject?.get("message")?.jsonObject
                ?: throw IllegalStateException("模型响应格式异常")
        }
    }

    // 清理模型输出标记
    private fun sanitize(raw: String): String {
        var text = raw
        // 移除整段 DSML 块
        text = Regex(
            "<\\s*\\|?\\s*DSML\\s*\\|?\\s*calls\\s*>.*?<\\s*/\\s*\\|?\\s*DSML\\s*\\|?\\s*calls\\s*>",
            RegexOption.DOT_MATCHES_ALL
        ).replace(text, "")
        // 移除零散标签
        text = Regex("<\\s*\\|?\\s*DSML[^>]*>", RegexOption.IGNORE_CASE).replace(text, "")
        text = Regex("</?\\s*(invoke|parameter|calls)\\b[^>]*>", RegexOption.IGNORE_CASE)
            .replace(text, "")
        text = Regex("<\\|[^>|]*\\|>").replace(text, "")
        return stripMarkdown(text).replace(Regex("\\n{3,}"), "\n\n").trim()
    }

    // 去除 Markdown 标记
    private fun stripMarkdown(raw: String): String = raw
        .replace(Regex("\\*\\*(.+?)\\*\\*"), "$1")
        .replace(Regex("__(.+?)__"), "$1")
        .replace(Regex("^\\s*#{1,6}\\s*", RegexOption.MULTILINE), "")
        .replace(Regex("^\\s*[-*+]\\s+", RegexOption.MULTILINE), "· ")
        .replace("`", "")
        .trim()

    private fun extractError(code: Int, raw: String): String {
        val detail = runCatching {
            json.parseToJsonElement(raw).jsonObject["error"]?.jsonObject
                ?.get("message")?.jsonPrimitive?.contentOrNull
        }.getOrNull()
        val hint = when (code) {
            401, 403 -> "，请检查 API Key"
            404 -> "，请检查接口地址与模型名"
            429 -> "，请求过于频繁或额度不足"
            else -> ""
        }
        return "HTTP $code$hint${detail?.let { "：$it" } ?: ""}"
    }
}
