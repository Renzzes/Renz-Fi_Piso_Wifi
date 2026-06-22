package com.renzfi.owner.util

import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

object DateUtils {
    private val formatter = SimpleDateFormat("MMM d, yyyy h:mm a", Locale.getDefault())

    fun formatTimestamp(timestamp: Long?): String {
        if (timestamp == null) return "Never"
        return formatter.format(Date(timestamp))
    }
}
