import org.gradle.api.publish.ivy.IvyPublication

plugins { `java-library`; alias(libs.plugins.kotlin.jvm); `ivy-publish` }
base { archivesName.set("${rootProject.name}-kotlin") }
kotlin { jvmToolchain(libs.versions.java.get().toInt()) }
dependencies {
    api(project(":java"))
    api(libs.coroutines)
    testImplementation(libs.kotlin.test)
    testRuntimeOnly(libs.junit.engine)
    testRuntimeOnly(libs.junit.launcher)
}
tasks.test {
    useJUnitPlatform()
    jvmArgs("--enable-native-access=ALL-UNNAMED")
}
tasks.jar {
    from(listOf("LICENSE", "THIRD_PARTY_NOTICES.md").map { rootProject.file("../$it") }) {
        into("META-INF/overwolf-hook")
    }
}
publishing.publications.create<IvyPublication>("kotlinApi") {
    module = base.archivesName.get()
    from(components["java"])
}
