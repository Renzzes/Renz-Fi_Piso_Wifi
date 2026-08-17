# Retrofit / Gson
-keepattributes Signature
-keepattributes *Annotation*
-keep class com.renzfi.owner.model.** { *; }
-dontwarn okhttp3.**
-dontwarn retrofit2.**

# Update system — Gson serialization for ReleaseManifest and GitHub API models
-keep class com.renzfi.owner.update.ReleaseManifest { *; }
-keep class com.renzfi.owner.firmware.FirmwareReleaseManifest { *; }
# GithubReleaseClient uses private inner data classes parsed by Gson
-keepclassmembers class com.renzfi.owner.update.GithubReleaseClient$* { *; }
