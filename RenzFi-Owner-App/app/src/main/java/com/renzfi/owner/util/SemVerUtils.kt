package com.renzfi.owner.util

/**
 * Minimal SemVer 2.0 comparator that handles:
 *   1.0.1 > 1.0.0
 *   1.1.0 > 1.0.9
 *   2.0.0 > 1.9.9
 *   1.0.1-beta.2 > 1.0.1-beta.1
 *   1.0.1        > 1.0.1-beta.3   (stable > any pre-release of same base)
 */
object SemVerUtils {

    private data class ParsedVersion(
        val major: Int,
        val minor: Int,
        val patch: Int,
        val preRelease: String?,
    )

    private fun parse(version: String): ParsedVersion? {
        val clean = version.trim().removePrefix("v")
        val dashIndex = clean.indexOf('-')
        val core = if (dashIndex >= 0) clean.substring(0, dashIndex) else clean
        val preRelease = if (dashIndex >= 0) clean.substring(dashIndex + 1).ifBlank { null } else null
        val parts = core.split('.')
        if (parts.size != 3) return null
        return try {
            ParsedVersion(
                major = parts[0].toInt(),
                minor = parts[1].toInt(),
                patch = parts[2].toInt(),
                preRelease = preRelease,
            )
        } catch (_: NumberFormatException) {
            null
        }
    }

    /**
     * Returns a positive int if [v1] > [v2], negative if [v1] < [v2], 0 if equal.
     * Unparseable versions are treated as equal to avoid accidental updates.
     */
    fun compare(v1: String, v2: String): Int {
        val a = parse(v1) ?: return 0
        val b = parse(v2) ?: return 0

        val majorDiff = a.major.compareTo(b.major)
        if (majorDiff != 0) return majorDiff

        val minorDiff = a.minor.compareTo(b.minor)
        if (minorDiff != 0) return minorDiff

        val patchDiff = a.patch.compareTo(b.patch)
        if (patchDiff != 0) return patchDiff

        return when {
            a.preRelease == null && b.preRelease == null -> 0
            a.preRelease == null -> 1    // stable > pre-release on same base
            b.preRelease == null -> -1   // pre-release < stable on same base
            else -> comparePreRelease(a.preRelease, b.preRelease)
        }
    }

    private fun comparePreRelease(pr1: String, pr2: String): Int {
        val num1 = pr1.substringAfterLast('.').toIntOrNull() ?: 0
        val num2 = pr2.substringAfterLast('.').toIntOrNull() ?: 0
        return num1.compareTo(num2)
    }

    /** Returns true if [candidate] is strictly newer than [current]. */
    fun isNewerThan(candidate: String, current: String): Boolean =
        compare(candidate, current) > 0
}
