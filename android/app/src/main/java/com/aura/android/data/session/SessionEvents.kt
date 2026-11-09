package com.aura.android.data.session

import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.SharedFlow
import java.util.concurrent.atomic.AtomicBoolean

// 会话过期事件
object SessionEvents {

    private val _expired = MutableSharedFlow<Unit>(extraBufferCapacity = 1)
    val expired: SharedFlow<Unit> = _expired

    private val notified = AtomicBoolean(false)

    fun notifyExpired() {
        if (notified.compareAndSet(false, true)) {
            _expired.tryEmit(Unit)
        }
    }

    // 重新登录后重置状态
    fun reset() {
        notified.set(false)
    }
}
