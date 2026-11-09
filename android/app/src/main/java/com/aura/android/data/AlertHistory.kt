package com.aura.android.data

import android.content.Context
import androidx.datastore.core.DataStore
import androidx.datastore.preferences.core.Preferences
import androidx.datastore.preferences.core.edit
import androidx.datastore.preferences.core.stringPreferencesKey
import androidx.datastore.preferences.preferencesDataStore
import com.aura.android.data.model.Alert
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.launch
import kotlinx.serialization.encodeToString
import kotlinx.serialization.json.Json

private val Context.alertHistoryStore: DataStore<Preferences> by preferencesDataStore(name = "aura_alert_history")

// 告警历史本地持久化
object AlertHistory {

    private const val MAX_ENTRIES = 200
    private val KEY = stringPreferencesKey("history_json")
    private val json = Json { ignoreUnknownKeys = true }
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)

    private var store: DataStore<Preferences>? = null

    private val _items = MutableStateFlow<List<Alert>>(emptyList())
    val items: StateFlow<List<Alert>> = _items

    fun init(context: Context) {
        if (store != null) return
        val target = context.applicationContext.alertHistoryStore
        store = target
        scope.launch {
            val raw = runCatching { target.data.first()[KEY] }.getOrNull()
            if (!raw.isNullOrBlank()) {
                runCatching {
                    _items.value = json.decodeFromString<List<Alert>>(raw)
                }
            }
        }
    }

    // 记录一条新告警
    fun record(alert: Alert) {
        if (store == null) return
        val current = _items.value
        if (current.any { it.type == alert.type && it.createdAt == alert.createdAt }) return
        val next = (listOf(alert) + current).take(MAX_ENTRIES)
        _items.value = next
        val target = store ?: return
        scope.launch {
            target.edit { prefs -> prefs[KEY] = json.encodeToString(next) }
        }
    }

    // 删除历史告警
    fun remove(alert: Alert) {
        if (store == null) return
        val current = _items.value
        val next = current.filterNot { it.type == alert.type && it.createdAt == alert.createdAt }
        if (next.size == current.size) return
        _items.value = next
        val target = store ?: return
        scope.launch {
            target.edit { prefs -> prefs[KEY] = json.encodeToString(next) }
        }
    }

    // 清空历史告警
    fun clear() {
        if (store == null) return
        if (_items.value.isEmpty()) return
        _items.value = emptyList()
        val target = store ?: return
        scope.launch {
            target.edit { prefs -> prefs.remove(KEY) }
        }
    }
}
