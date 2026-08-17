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

    fun formatRelativeTime(timestamp: Long): String {
        val deltaMs = System.currentTimeMillis() - timestamp
        val minutes = deltaMs / 60_000
        return when {
            minutes < 1 -> "just now"
            minutes < 60 -> "${minutes}m ago"
            minutes < 1440 -> "${minutes / 60}h ago"
            else -> formatTimestamp(timestamp)
        }
    }
}
