plugins { application }
java { toolchain.languageVersion.set(JavaLanguageVersion.of(25)) }

dependencies {
    implementation("dev.sxlver.native:overwolf-hook:@OWH_SDK_VERSION@")
}

application {
    mainClass.set("SdkCheck")
    applicationDefaultJvmArgs = listOf("--enable-native-access=ALL-UNNAMED")
}
