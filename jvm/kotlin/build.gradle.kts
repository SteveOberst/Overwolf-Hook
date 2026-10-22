plugins {
    kotlin("jvm") version "2.4.0"
    `java-library`
    `ivy-publish`
}
group = "dev.sxlver.native"
version = file("../../version.txt").readText().trim()
kotlin { jvmToolchain(25) }
dependencies {
    api("dev.sxlver.native:overwolf-hook:$version:windows-x64@jar")
    api("org.jetbrains.kotlinx:kotlinx-coroutines-core:1.11.0")
    testImplementation(kotlin("test-junit5"))
    testRuntimeOnly("org.junit.jupiter:junit-jupiter-engine:6.0.3")
    testRuntimeOnly("org.junit.platform:junit-platform-launcher:6.0.3")
}
tasks.test {
    useJUnitPlatform()
    jvmArgs("--enable-native-access=ALL-UNNAMED")
    providers.gradleProperty("owhCaptureDll").orNull?.let { systemProperty("owhCaptureDll", it) }
}
tasks.jar {
    from(files("../../LICENSE", "../../THIRD_PARTY_NOTICES.md")) { into("META-INF/overwolf-hook") }
}
// GitHub assets have an Ivy descriptor so consumers receive SDK/coroutine dependencies automatically.
publishing {
    publications {
        create<IvyPublication>("sdk") { from(components["java"]) }
    }
    repositories {
        ivy {
            name = "staging"
            url = layout.buildDirectory.dir("repository").get().asFile.toURI()
            patternLayout {
                artifact("v[revision]/[artifact]-[revision](-[classifier]).[ext]")
                ivy("v[revision]/[module]-[revision].ivy.xml")
            }
        }
    }
}
tasks.withType<GenerateModuleMetadata>().configureEach { enabled = false }
