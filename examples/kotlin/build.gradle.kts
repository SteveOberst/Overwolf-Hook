plugins {
    kotlin("jvm") version "2.4.0"
    application
}
kotlin { jvmToolchain(25) }
val sdkVersion = providers.gradleProperty("overwolfHookVersion").orElse("0.5.0-beta.1")
dependencies {
    implementation("dev.sxlver.native:overwolf-hook-kotlin:${sdkVersion.get()}")
}
application {
    mainClass.set("ReadStateKt")
    applicationDefaultJvmArgs = listOf("--enable-native-access=ALL-UNNAMED")
}
