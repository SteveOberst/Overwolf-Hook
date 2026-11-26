plugins {
    kotlin("jvm") version "2.4.0"
    application
}
kotlin { jvmToolchain(25) }
dependencies {
    implementation("dev.sxlver.native:overwolf-hook-kotlin:${providers.gradleProperty("sdkVersion").get()}")
}
application {
    mainClass.set(providers.gradleProperty("exampleMainClass").orElse("FirstOverlayKt"))
    applicationDefaultJvmArgs = listOf("--enable-native-access=ALL-UNNAMED")
}
