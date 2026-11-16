package com.aura.android.data

import android.Manifest
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
import androidx.core.app.NotificationCompat
import androidx.core.app.NotificationManagerCompat
import androidx.core.content.ContextCompat
import com.aura.android.MainActivity

// 雷达连接异常时给手机发系统通知
object AlertNotifier {

    private const val CHANNEL_ID = "aura_device_status"
    private const val NOTIFICATION_ID = 4101

    private var appContext: Context? = null

    // 由设置页控制，跟着 DataStore 走
    @Volatile
    var enabled: Boolean = true

    fun init(context: Context) {
        val ctx = context.applicationContext
        appContext = ctx
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val channel = NotificationChannel(
                CHANNEL_ID,
                "设备状态",
                NotificationManager.IMPORTANCE_HIGH
            ).apply {
                description = "雷达断开连接或数据停止更新时提醒"
            }
            ctx.getSystemService(NotificationManager::class.java)
                ?.createNotificationChannel(channel)
        }
    }

    fun post(title: String, message: String) {
        if (!enabled) return
        val ctx = appContext ?: return
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU &&
            ContextCompat.checkSelfPermission(
                ctx, Manifest.permission.POST_NOTIFICATIONS
            ) != PackageManager.PERMISSION_GRANTED
        ) {
            return
        }
        val intent = Intent(ctx, MainActivity::class.java).apply {
            flags = Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TOP
        }
        val pending = PendingIntent.getActivity(
            ctx, 0, intent,
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
        )
        val notification = NotificationCompat.Builder(ctx, CHANNEL_ID)
            .setSmallIcon(android.R.drawable.stat_notify_error)
            .setContentTitle(title)
            .setContentText(message)
            .setStyle(NotificationCompat.BigTextStyle().bigText(message))
            .setPriority(NotificationCompat.PRIORITY_HIGH)
            .setCategory(NotificationCompat.CATEGORY_ALARM)
            .setAutoCancel(true)
            .setContentIntent(pending)
            .build()
        try {
            NotificationManagerCompat.from(ctx).notify(NOTIFICATION_ID, notification)
        } catch (_: SecurityException) {
            // 用户在系统设置里撤销了通知权限，忽略即可
        }
    }

    fun cancel() {
        val ctx = appContext ?: return
        NotificationManagerCompat.from(ctx).cancel(NOTIFICATION_ID)
    }
}
