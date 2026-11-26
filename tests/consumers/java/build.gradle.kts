plugins { application }

java { toolchain.languageVersion.set(JavaLanguageVersion.of(25)) }

dependencies {
    implementation("dev.sxlver.native:overwolf-hook:${providers.gradleProperty("sdkVersion").get()}") {
        artifact { name = "overwolf-hook"; type = "jar"; extension = "jar"; classifier = "windows-x64" }
    }
}

application {
    mainClass.set("ConsumerSmoke")
    applicationDefaultJvmArgs = listOf("--enable-native-access=ALL-UNNAMED")
}
