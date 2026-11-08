package com.aura.android.ui.theme

import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Shapes
import androidx.compose.ui.unit.dp

// 应用圆角规范
val AuraShapes = Shapes(
    small = RoundedCornerShape(12.dp),
    medium = RoundedCornerShape(18.dp),
    large = RoundedCornerShape(24.dp)
)

// 柱端胶囊圆角
val BarCapShape = RoundedCornerShape(topStart = 4.dp, topEnd = 4.dp)
