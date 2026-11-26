plugins { application }
java { toolchain.languageVersion.set(JavaLanguageVersion.of(25)) }
dependencies {
    implementation("dev.sxlver.native:overwolf-hook:${providers.gradleProperty("sdkVersion").get()}")
}
application {
    mainClass.set(providers.gradleProperty("exampleMainClass").orElse("FirstOverlay"))
    applicationDefaultJvmArgs = listOf("--enable-native-access=ALL-UNNAMED")
}
