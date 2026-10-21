plugins {
    kotlin("jvm") version "2.4.0"
    application
}
repositories { mavenCentral() }
kotlin { jvmToolchain(25) }
dependencies { implementation(files(providers.gradleProperty("overwolfHookJar"))) }
application {
    mainClass.set("ReadStateKt")
    applicationDefaultJvmArgs = listOf("--enable-native-access=ALL-UNNAMED")
}
