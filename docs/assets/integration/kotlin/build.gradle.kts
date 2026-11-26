plugins {
    kotlin("jvm") version "2.4.0"
    application
}
kotlin { jvmToolchain(25) }

dependencies {
    implementation("dev.sxlver.native:overwolf-hook-kotlin:@OWH_SDK_VERSION@")
}

application {
    mainClass.set("SdkCheckKt")
    applicationDefaultJvmArgs = listOf("--enable-native-access=ALL-UNNAMED")
}
