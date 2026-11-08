package com.aura.android.ui.screens.chat

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.lifecycle.ViewModel
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewModelScope
import com.aura.android.data.ai.AiClient
import com.aura.android.data.ai.AiConfig
import com.aura.android.data.ai.SearchResult
import com.aura.android.data.ai.VitalsContext
import com.aura.android.data.ai.VitalsSnapshot
import com.aura.android.data.datastore.TokenDataStore
import com.aura.android.data.model.ChatMessage
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.launch

// 一条对话消息
data class ChatItem(
    val role: String,
    val content: String,
    val tag: String? = null,
    val sources: List<SearchResult> = emptyList()
)

class ChatViewModel(
    private val tokenDataStore: TokenDataStore
) : ViewModel() {

    val messages = mutableStateListOf<ChatItem>()

    var isLoading by mutableStateOf(false)
        private set

    var inputText by mutableStateOf("")
        private set

    var aiConfig by mutableStateOf(AiConfig())
        private set

    var snapshot by mutableStateOf<VitalsSnapshot?>(null)
        private set

    // 联网搜索开关
    var webSearchEnabled by mutableStateOf(true)
        private set

    fun toggleWebSearch() {
        webSearchEnabled = !webSearchEnabled
    }

    // 顶部展示的模型名
    val modelLabel: String
        get() = if (aiConfig.isConfigured) aiConfig.model else "本地分析"

    val configured: Boolean
        get() = aiConfig.isConfigured

    init {
        messages.add(
            ChatItem(
                role = "assistant",
                content = "您好，我是 AURA 健康助手。我会先读取当前的心率、呼吸、专注度、压力与疲劳数据，再结合它们回答您的问题。",
                tag = null
            )
        )
        reload()
    }

    // 刷新接口配置与数据快照
    fun reload() {
        viewModelScope.launch {
            val baseUrl = tokenDataStore.aiBaseUrl.first()
            val apiKey = tokenDataStore.aiApiKey.first()
            val model = tokenDataStore.aiModel.first()
            aiConfig = AiConfig(
                baseUrl = baseUrl ?: AiConfig.DEFAULT_BASE_URL,
                apiKey = apiKey.orEmpty(),
                model = model ?: AiConfig.DEFAULT_MODEL
            ).normalized()
            snapshot = VitalsContext.load()
        }
    }

    fun updateInput(text: String) {
        inputText = text
    }

    fun sendMessage() {
        val text = inputText.trim()
        if (text.isEmpty() || isLoading) return

        messages.add(ChatItem(role = "user", content = text))
        inputText = ""
        isLoading = true

        viewModelScope.launch {
            val data = snapshot ?: VitalsContext.load().also { snapshot = it }
            try {
                if (data == null) {
                    messages.add(
                        ChatItem(
                            role = "assistant",
                            content = "暂时读不到体征数据，请先在首页刷新一次再问我。",
                            tag = "本地分析"
                        )
                    )
                    return@launch
                }
                val reply = if (aiConfig.isConfigured) {
                    requestModel(text, data)
                } else {
                    null
                }
                val content = reply?.content ?: VitalsContext.localAnalysis(text, data)
                val tag = when {
                    reply == null -> "本地分析"
                    reply.sources.isNotEmpty() -> "${aiConfig.model} · 联网"
                    else -> aiConfig.model
                }
                messages.add(
                    ChatItem(
                        role = "assistant",
                        content = content,
                        tag = tag,
                        sources = reply?.sources.orEmpty()
                    )
                )
            } catch (e: Exception) {
                // 接口失败时回退本地分析
                val fallback = if (data != null) VitalsContext.localAnalysis(text, data) else null
                val reason = e.message ?: "未知错误"
                messages.add(
                    ChatItem(
                        role = "assistant",
                        content = buildString {
                            appendLine("调用 ${aiConfig.model} 失败：$reason")
                            if (fallback != null) {
                                appendLine()
                                appendLine("以下为本地分析（未使用接口）：")
                                append(fallback)
                            }
                        }.trim(),
                        tag = "接口异常"
                    )
                )
            } finally {
                isLoading = false
            }
        }
    }

    private suspend fun requestModel(question: String, data: VitalsSnapshot): com.aura.android.data.ai.AiReply {
        val history = messages
            .dropLast(1)
            .filter { it.content.isNotBlank() }
            .takeLast(6)
            .map { ChatMessage(role = it.role, content = it.content) }
        val payload = buildList {
            add(ChatMessage(role = "system", content = VitalsContext.systemPrompt(data)))
            addAll(history.filter { it.role == "user" || it.role == "assistant" })
            add(ChatMessage(role = "user", content = question))
        }
        return AiClient.chat(aiConfig, payload, allowWebSearch = webSearchEnabled)
    }

    fun sendQuickQuestion(question: String) {
        if (isLoading) return
        inputText = question
        sendMessage()
    }

    class Factory(
        private val tokenDataStore: TokenDataStore
    ) : ViewModelProvider.Factory {
        @Suppress("UNCHECKED_CAST")
        override fun <T : ViewModel> create(modelClass: Class<T>): T {
            return ChatViewModel(tokenDataStore) as T
        }
    }
}
