# JNI entry points are looked up by name at runtime.
-keepclasseswithmembernames class org.audioanalyzer.core.audio.NativeEngine {
    native <methods>;
}
