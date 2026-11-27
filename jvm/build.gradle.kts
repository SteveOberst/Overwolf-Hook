plugins {
    base
    alias(libs.plugins.kotlin.jvm) apply false
}

val sdkVersion = file("../version.txt").readText().trim()
providers.gradleProperty("owhBuildDirectory").orNull?.let { layout.buildDirectory.set(file(it)) }
allprojects {
    group = "dev.sxlver.native"
    version = sdkVersion
}
subprojects {
    layout.buildDirectory.set(rootProject.layout.buildDirectory.dir(name))
    plugins.withId("ivy-publish") {
        configure<PublishingExtension> {
            repositories {
                ivy {
                    name = "sdkStaging"
                    url = rootProject.layout.buildDirectory.dir("repository").get().asFile.toURI()
                    patternLayout {
                        artifact("v[revision]/[artifact]-[revision](-[classifier]).[ext]")
                        ivy("v[revision]/[module]-[revision].ivy.xml")
                    }
                }
            }
        }
    }
}
tasks.check { dependsOn(":java:check", ":kotlin:check") }
tasks.assemble { dependsOn(":java:assemble", ":kotlin:assemble") }
tasks.register("publish") { dependsOn(":java:publish", ":kotlin:publish") }
