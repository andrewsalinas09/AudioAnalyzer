plugins {
    alias(libs.plugins.android.library)
}

android {
    namespace = "org.audioanalyzer.core.audio"
    compileSdk {
        version = release(36) {
            minorApiLevel = 1
        }
    }
    // Pinned toolchain versions — see docs/building.md before changing.
    ndkVersion = "28.2.13676358"

    defaultConfig {
        minSdk = 31
        ndk {
            abiFilters += listOf("arm64-v8a", "x86_64")
        }
        externalNativeBuild {
            cmake {
                // Oboe's prefab package links against the shared C++ runtime.
                arguments += "-DANDROID_STL=c++_shared"
            }
        }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.31.6"
        }
    }

    buildFeatures {
        prefab = true
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_11
        targetCompatibility = JavaVersion.VERSION_11
    }
}

dependencies {
    implementation(libs.oboe)
    implementation(libs.androidx.core.ktx)
}
