# Android Release Signing

This document covers keystore management, release signing, and the GitHub
release workflow for the Renz-Fi Manager Android app (`RenzFi-Owner-App`).

---

## Critical rule

**All production APKs must be signed with the same keystore.**

Android validates the signing certificate on every install. If the keystore
changes between releases, Android will reject the update and require the user
to manually uninstall — which erases all saved devices and preferences.

**Treat the keystore like a password. Lose it and you lose the ability to update.**

---

## Keystore file

| Property | Value |
|----------|-------|
| Filename | `renzfi-release.keystore` |
| Location (local) | Outside version control — e.g. `%USERPROFILE%\keys\renzfi-release.keystore` |
| Format | JKS or PKCS12 (Android Gradle supports both) |
| Recommended validity | 25–30 years (Android Play requirement; use the same for sideloaded APKs) |

### Generate (one-time)

```powershell
keytool -genkeypair `
  -alias renzfi-release `
  -keyalg RSA `
  -keysize 4096 `
  -validity 10000 `
  -keystore renzfi-release.keystore `
  -storepass YOUR_STORE_PASSWORD `
  -keypass YOUR_KEY_PASSWORD `
  -dname "CN=Renz-Fi Manager, OU=Renz-Fi, O=Renz-Fi, L=Philippines, C=PH"
```

Replace `YOUR_STORE_PASSWORD` and `YOUR_KEY_PASSWORD` with strong, unique passwords.

---

## keystore.properties

Create `RenzFi-Owner-App/keystore.properties` (already in `.gitignore`):

```properties
storeFile=/absolute/path/to/renzfi-release.keystore
storePassword=YOUR_STORE_PASSWORD
keyAlias=renzfi-release
keyPassword=YOUR_KEY_PASSWORD
```

`app/build.gradle.kts` reads this file for `signingConfigs.release`.
If the file does not exist, the release build will be unsigned (do not ship it).

---

## Backup strategy

### Local backup

1. Copy `renzfi-release.keystore` to at least two offline locations:
   - Encrypted USB drive stored separately from your PC
   - Password-manager attachment (Bitwarden, KeePass, 1Password) with the
     keystore file attached and the passwords recorded

2. Store `keystore.properties` content (passwords) in the same password manager.

### Cloud backup (optional)

Upload a **GPG-encrypted** copy to private cloud storage. Do not store the
unencrypted keystore in any cloud service.

```bash
gpg --symmetric --cipher-algo AES256 renzfi-release.keystore
# Produces renzfi-release.keystore.gpg — safe to store remotely
```

---

## Recovery strategy

| Scenario | Impact | Resolution |
|----------|--------|------------|
| Keystore file lost, passwords known | Cannot sign releases | Restore from backup |
| Keystore file + passwords lost | **Cannot update existing installs** | Generate new keystore; users must uninstall + reinstall (data loss) |
| Passwords lost, keystore exists | Cannot sign releases | Restore from backup |
| Keystore compromised | Attacker can ship fake updates | Generate new keystore immediately; publish security advisory; users reinstall |

There is no Android mechanism to rotate the signing key on existing installs
without requiring an uninstall. **Backup the keystore. Do not lose it.**

---

## Signing the release APK

### Manually (local build)

```powershell
# In RenzFi-Owner-App/
$env:JAVA_HOME = "C:\Program Files\Android\Android Studio\jbr"
.\gradlew.bat assembleRelease
# Output: app/build/outputs/apk/release/renzfi-manager-release.apk
```

Gradle reads `keystore.properties` automatically and applies `signingConfigs.release`.

### Verify the signature

```bash
apksigner verify --print-certs app/build/outputs/apk/release/renzfi-manager-release.apk
```

The certificate subject should match what was set during keystore generation.

---

## versionCode rule

Every public release **must** increment `versionCode` in `app/build.gradle.kts`:

```kotlin
versionCode = 2   // was 1 — must be higher than the installed version
versionName = "1.0.1"
```

Android uses `versionCode` (integer), not `versionName` (string), to decide
whether an update is valid. If `versionCode` stays the same, the installer
will refuse to update.

---

## GitHub Release process

### 1. Prepare

- Increment `versionCode` and `versionName` in `app/build.gradle.kts`
- Build and sign the release APK (see above)
- Compute SHA-256:

```powershell
Get-FileHash app\build\outputs\apk\release\renzfi-manager-release.apk -Algorithm SHA256
```

### 2. Create `version.json`

```json
{
  "version": "1.0.1",
  "channel": "stable",
  "apkUrl": "https://github.com/clareenz/Renz-Fi_Piso_Wifi/releases/download/manager-android%2Fv1.0.1/RenzFi-Manager-v1.0.1.apk",
  "sha256": "PASTE_LOWERCASE_SHA256_HERE",
  "publishedAt": "2026-06-23T08:00:00Z",
  "releaseNotes": "Brief description of what changed."
}
```

### 3. Create `release-notes.md`

Plain-text changelog visible in the GitHub Release body and shown in the
app's Update Available dialog.

### 4. Publish the GitHub Release

1. Go to **Releases → Create a new release**
2. Tag: `manager-android/v1.0.1` (must match `Constants.GITHUB_TAG_PREFIX + versionName`)
3. Release title: `Renz-Fi Manager v1.0.1`
4. Upload assets:
   - `RenzFi-Manager-v1.0.1.apk`
   - `version.json`
   - `release-notes.md`
5. Check **"Set as a pre-release"** for beta; leave unchecked for stable

### 5. Verify

- Open the app on a device running the previous version
- Go to **Settings → Check for Updates** or **About → Check for Updates**
- The update dialog should appear within seconds

---

## Beta releases

Use tag `manager-android/v1.1.0-beta.1` and check **Set as pre-release** on
GitHub. Beta APKs must still be signed with the same release keystore.

Users opt into beta via **About → Release Channel → Beta**.

---

## GitHub Actions CI (future automation)

Store the keystore and passwords as GitHub Actions secrets:

| Secret | Content |
|--------|---------|
| `RELEASE_KEYSTORE_BASE64` | `base64 -w0 renzfi-release.keystore` |
| `RELEASE_STORE_PASSWORD` | Store password |
| `RELEASE_KEY_ALIAS` | `renzfi-release` |
| `RELEASE_KEY_PASSWORD` | Key password |

CI workflow skeleton (`release-manager-android.yml`):

```yaml
on:
  push:
    tags: ['manager-android/v*']

jobs:
  build:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - uses: actions/setup-java@v4
        with: { java-version: '17', distribution: 'temurin' }
      - name: Decode keystore
        run: |
          echo "${{ secrets.RELEASE_KEYSTORE_BASE64 }}" | base64 -d \
            > RenzFi-Owner-App/renzfi-release.keystore
      - name: Write keystore.properties
        run: |
          cat > RenzFi-Owner-App/keystore.properties <<EOF
          storeFile=../renzfi-release.keystore
          storePassword=${{ secrets.RELEASE_STORE_PASSWORD }}
          keyAlias=${{ secrets.RELEASE_KEY_ALIAS }}
          keyPassword=${{ secrets.RELEASE_KEY_PASSWORD }}
          EOF
      - name: Build release APK
        working-directory: RenzFi-Owner-App
        run: ./gradlew assembleRelease
      - name: Upload to GitHub Release
        uses: softprops/action-gh-release@v2
        with:
          files: RenzFi-Owner-App/app/build/outputs/apk/release/renzfi-manager-release.apk
```

Attach `version.json` and `release-notes.md` manually or generate them in CI.
