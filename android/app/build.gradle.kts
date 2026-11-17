plugins {
    id("com.android.application")
}

/*
 * This Gradle project does NOT drive CMake. The runner is built separately
 * with CMake and the NDK, as a shared library (libnds_runner.so, holding
 * SDL_main) next to libSDL3.so; Gradle only packages the prebuilt .so files
 * into an installable APK.
 *
 * Before running Gradle, stage them in android/app/jniLibs/arm64-v8a/. The
 * release workflow does that by itself.
 */

/*
 * The version comes from the newest v* tag reachable from HEAD, so a CI build
 * and a local build of the same commit agree, and it is the one the runner's
 * self-updater compares releases with (NDS_RELEASE_VERSION). Override with
 * -PappVersionName= or NDS_VERSION_NAME when git history is unavailable.
 */
val appVersionName: String = run {
    val override = (project.findProperty("appVersionName") as String?)
        ?: System.getenv("NDS_VERSION_NAME")
    if (!override.isNullOrBlank()) return@run override.removePrefix("v")
    try {
        val proc = ProcessBuilder("git", "describe", "--tags", "--abbrev=0", "--match", "v*")
            .directory(rootProject.projectDir)
            .redirectErrorStream(true)
            .start()
        val out = proc.inputStream.bufferedReader().readText().trim()
        if (proc.waitFor() == 0 && out.isNotEmpty()) out.removePrefix("v") else "0.0.0"
    } catch (e: Exception) {
        "0.0.0"
    }
}

// Packed major/minor/patch so it rises with every release; Android refuses to
// install an APK whose versionCode is lower than the installed one.
val appVersionCode: Int = run {
    val parts = Regex("""^(\d+)\.(\d+)\.(\d+)""").find(appVersionName)?.destructured
        ?: return@run 1
    val (major, minor, patch) = parts
    maxOf(1, major.toInt() * 1_000_000 + minor.toInt() * 1_000 + patch.toInt())
}

android {
    namespace = "com.birabittoh.dq9recompiled"
    compileSdk = 35

    defaultConfig {
        applicationId = "com.birabittoh.dq9recompiled"
        minSdk = 28
        targetSdk = 35
        versionCode = appVersionCode
        versionName = appVersionName

        ndk {
            // Only arm64 is built.
            abiFilters += "arm64-v8a"
        }
    }

    /*
     * Stable sideload signing key.
     *
     * A machine's debug keystore makes every build a different signer, so
     * installing over an existing copy (which is what the self-updater asks
     * the package installer to do) fails with INSTALL_FAILED_UPDATE_INCOMPATIBLE
     * and the app has to be uninstalled first, taking its saves with it.
     *
     * The keystore is not in the repository (see .gitignore). Create one with:
     *
     *   keytool -genkeypair -keystore android/keystore/sideload.jks \
     *     -storetype PKCS12 -storepass dq9recomp -keypass dq9recomp \
     *     -alias dq9recomp -keyalg RSA -keysize 2048 -validity 10950 \
     *     -dname "CN=DQ9Recomp Sideload"
     *
     * Path, alias and passwords are overridable through gradle properties
     * (-PsideloadKeystore=...) or DQ9_KEYSTORE / DQ9_KEYSTORE_PASSWORD /
     * DQ9_KEY_ALIAS / DQ9_KEY_PASSWORD in the environment. CI restores the
     * keystore from the DQ9_KEYSTORE_BASE64 secret. Without a keystore the
     * build falls back to debug signing.
     */
    // CI exports these from repository secrets, which arrive as empty strings
    // rather than unset when a secret does not exist, so blanks fall through.
    fun signingSetting(property: String, env: String, fallback: String): String =
        listOf(project.findProperty(property) as String?, System.getenv(env), fallback)
            .firstOrNull { !it.isNullOrBlank() }!!

    // Resolved against android/, not app/.
    val sideloadKeystore = rootProject.file(
        signingSetting("sideloadKeystore", "DQ9_KEYSTORE", "keystore/sideload.jks")
    )

    signingConfigs {
        if (sideloadKeystore.isFile) {
            create("sideload") {
                storeFile = sideloadKeystore
                storePassword = signingSetting(
                    "sideloadKeystorePassword", "DQ9_KEYSTORE_PASSWORD", "dq9recomp")
                keyAlias = signingSetting(
                    "sideloadKeyAlias", "DQ9_KEY_ALIAS", "dq9recomp")
                keyPassword = signingSetting(
                    "sideloadKeyPassword", "DQ9_KEY_PASSWORD", "dq9recomp")
            }
        }
    }

    // Prebuilt native libraries, staged into jniLibs/arm64-v8a/.
    sourceSets["main"].jniLibs.srcDirs(
        layout.projectDirectory.dir("jniLibs")
    )

    // The launcher icon's foreground is the ROM's banner icon, so it is
    // generated (scripts/gen-android-icon.py), never committed.
    val iconRes = layout.projectDirectory.dir("icon-res").asFile
    require(iconRes.resolve("mipmap-xxxhdpi/ic_launcher_foreground.png").isFile) {
        "Launcher icon not generated: run python scripts/gen-android-icon.py"
    }
    sourceSets["main"].res.srcDirs(iconRes)

    /*
     * SDL3's Android Java glue (org.libsdl.app.*) comes from the SDL3 source
     * tree libSDL3.so was built from, not from this repository: the native
     * side registers its methods against these exact classes, so a copy
     * that drifts from the library aborts the app in System.loadLibrary.
     *
     * Point -PsdlJavaDir=... or SDL3_JAVA_DIR at
     * <SDL3 source>/android-project/app/src/main/java.
     */
    val sdlJavaDir = rootProject.file(
        (project.findProperty("sdlJavaDir") as String?)
            ?: System.getenv("SDL3_JAVA_DIR")
            ?: "../build-sdl3-android/SDL/android-project/app/src/main/java"
    )
    require(sdlJavaDir.isDirectory) {
        "SDL Java glue not found at $sdlJavaDir: set SDL3_JAVA_DIR to " +
            "<SDL3 source>/android-project/app/src/main/java"
    }
    sourceSets["main"].java.srcDirs(sdlJavaDir)

    buildTypes {
        release {
            isMinifyEnabled = false
            signingConfig = signingConfigs.findByName("sideload")
                ?: signingConfigs.getByName("debug")
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    packaging {
        jniLibs.keepDebugSymbols += "**/*.so"
        jniLibs.useLegacyPackaging = true
    }
}

/*
 * Discord's Social SDK, for Rich Presence (the runner links the same .aar's
 * libdiscord_partner_sdk.so, CMake NDS_DISCORD_SOCIAL_SDK_AAR). It is not in
 * the repository: it comes from the Discord Developer Portal
 * (DiscordSocialSdk-<version>.zip, lib/release/discord_partner_sdk.aar), and
 * CI fetches it from the private assets release like the ROM. Point
 * -PdiscordSdkAar=... or DISCORD_SOCIAL_SDK_AAR at it, or place it at
 * app/libs/discord_partner_sdk.aar. Without it the APK has no Discord
 * activity (DQ9RecompiledActivity checks for the SDK's classes).
 */
val discordSdkAar = rootProject.file(
    listOf(project.findProperty("discordSdkAar") as String?,
           System.getenv("DISCORD_SOCIAL_SDK_AAR"))
        .firstOrNull { !it.isNullOrBlank() } ?: "app/libs/discord_partner_sdk.aar"
)

dependencies {
    // FileProvider only; the self-updater resolves it through the activity's
    // class loader (ndsrecomp/runner/src/update/self_update_android.cpp).
    implementation("androidx.core:core:1.13.1")
    // Custom Tabs: the Discord sign-in page opens over the game rather than
    // in a full browser (DQ9RecompiledActivity.openCustomTab).
    implementation("androidx.browser:browser:1.8.0")
    if (discordSdkAar.isFile) {
        implementation(files(discordSdkAar))
    }
}
