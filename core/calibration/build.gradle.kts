// Pure-JVM module: no Android dependencies, so the parser is unit-testable
// on the host and reusable outside the app.
plugins {
    alias(libs.plugins.kotlin.jvm)
}

dependencies {
    testImplementation(libs.junit)
}
