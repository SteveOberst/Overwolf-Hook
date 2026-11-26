import org.gradle.api.publish.ivy.IvyPublication
import org.gradle.api.publish.ivy.tasks.PublishToIvyRepository

plugins {
    `java-library`
    kotlin("jvm") version "2.4.0"
    `ivy-publish`
}

group = "dev.sxlver.native"
version = file("../version.txt").readText().trim()
providers.gradleProperty("owhBuildDirectory").orNull?.let { layout.buildDirectory.set(file(it)) }
java { toolchain.languageVersion.set(JavaLanguageVersion.of(25)) }
kotlin { jvmToolchain(25) }

// One project, two language surfaces. Java remains JDK-only; the optional Kotlin
// artifact depends on that same core and owns no second resource hierarchy.
val generatedBindings = layout.buildDirectory.dir("generated/sources/ffm")
val publicHeaderManifest = file("../cmake/public-headers.txt")
val publicHeaders = publicHeaderManifest.readLines().filter { it.isNotBlank() }
    .map { file("../include/overwolf_hook/$it") }
val generateBindings by tasks.registering(Exec::class) {
    inputs.files(publicHeaderManifest, publicHeaders,
        file("tools/generate_bindings.py"), file("tools/jextract.json"),
        fileTree("../tools/bindings") { include("**/*.py", "**/*.h") })
    inputs.property("jextract", providers.gradleProperty("jextract").orElse("pinned"))
    outputs.dir(generatedBindings)
    doFirst {
        val arguments = mutableListOf(providers.gradleProperty("pythonExecutable").orElse("python").get(),
            file("tools/generate_bindings.py").absolutePath, "--output", generatedBindings.get().asFile.absolutePath)
        providers.gradleProperty("jextract").orNull?.let { arguments.addAll(listOf("--jextract", it)) }
        commandLine(arguments)
    }
}

val main by sourceSets.getting {
    java.setSrcDirs(listOf("java/src/main/java"))
    java.srcDir(generateBindings.map { generatedBindings })
}
val kotlinApi by sourceSets.creating {
    compileClasspath += main.output
    runtimeClasspath += main.output
}
val test by sourceSets.getting {
    java.setSrcDirs(listOf("java/src/test/java"))
    compileClasspath += kotlinApi.output
    runtimeClasspath += kotlinApi.output
}
kotlin.sourceSets.named("main") { kotlin.setSrcDirs(emptyList<String>()) }
kotlin.sourceSets.named("kotlinApi") { kotlin.setSrcDirs(listOf("kotlin/src/main/kotlin")) }
kotlin.sourceSets.named("test") { kotlin.setSrcDirs(listOf("kotlin/src/test/kotlin")) }
kotlin.target.compilations.named("kotlinApi") { associateWith(kotlin.target.compilations.getByName("main")) }
kotlin.target.compilations.named("test") { associateWith(kotlin.target.compilations.getByName("kotlinApi")) }

dependencies {
    add(kotlinApi.implementationConfigurationName, "org.jetbrains.kotlin:kotlin-stdlib:2.4.0")
    add(kotlinApi.implementationConfigurationName, "org.jetbrains.kotlinx:kotlinx-coroutines-core:1.11.0")
    testImplementation(kotlin("test-junit5"))
    testRuntimeOnly("org.junit.jupiter:junit-jupiter-engine:6.0.3")
    testRuntimeOnly("org.junit.platform:junit-platform-launcher:6.0.3")
}
configurations.named(test.implementationConfigurationName) {
    extendsFrom(configurations.getByName(kotlinApi.implementationConfigurationName))
}

// Native compilation stays in CMake. Both entry points consume the identical
// DLL pair, e.g. ./gradlew build -PnativeDirectory=../build/windows-x64/native/Release.
val nativeDirectory = providers.gradleProperty("nativeDirectory").map { file(it) }
tasks.processResources {
    from(nativeDirectory) {
        include("owh_client.dll", "owh_agent.dll")
        into("overwolf/native/windows-x64")
    }
    from(files("../LICENSE", "../THIRD_PARTY_NOTICES.md", "../third_party/MinHook.LICENSE.txt", "../third_party/Boost.LICENSE.txt")) {
        into("META-INF/overwolf-hook")
    }
    doFirst {
        val directory = nativeDirectory.orNull
            ?: throw GradleException("Set -PnativeDirectory to the CMake output containing the matching Client/Agent DLLs")
        for (name in listOf("owh_client.dll", "owh_agent.dll"))
            if (!directory.resolve(name).isFile) throw GradleException("Missing native payload: ${directory.resolve(name)}")
    }
}
tasks.jar {
    archiveClassifier.set("windows-x64")
    archiveFileName.set("overwolf-hook-$version-abi-windows-x64.jar")
    manifest.attributes("Automatic-Module-Name" to "dev.sxlver.overwolfhook",
        "Implementation-Version" to version, "Overwolf-ABI-Version" to "1")
}
val kotlinApiJar by tasks.registering(Jar::class) {
    archiveBaseName.set("overwolf-hook-kotlin")
    from(kotlinApi.output)
    from(files("../LICENSE", "../THIRD_PARTY_NOTICES.md")) { into("META-INF/overwolf-hook") }
}
tasks.assemble { dependsOn(kotlinApiJar) }

tasks.test {
    useJUnitPlatform()
    jvmArgs("--enable-native-access=ALL-UNNAMED")
}
fun registerSmoke(name: String, entry: String) = tasks.register<JavaExec>(name) {
    dependsOn(tasks.testClasses, tasks.jar)
    // Exercise the actual packaged core, including native extraction, without
    // Kotlin on its classpath. Java users must remain JDK-only consumers.
    classpath = files(tasks.jar.flatMap { it.archiveFile }, test.output)
    javaLauncher.set(javaToolchains.launcherFor(java.toolchain))
    mainClass.set(entry)
    jvmArgs("--enable-native-access=ALL-UNNAMED")
}
val artifactTest = registerSmoke("artifactTest", "JvmSmokeTest")
val nativeLoaderTest = registerSmoke("nativeLoaderTest", "NativeLoaderTest").apply {
    configure { args(layout.buildDirectory.dir("native-loader-test").get().asFile.absolutePath) }
}
val overlayContracts = registerSmoke("overlayContracts", "dev.sxlver.overwolfhook.internal.OverlayContractTest")
val cacheTest by tasks.registering(Exec::class) {
    dependsOn(tasks.testClasses, tasks.jar)
    doFirst {
        commandLine(providers.gradleProperty("pythonExecutable").orElse("python").get(),
            file("../tests/jvm_cache_test.py").absolutePath,
            javaToolchains.launcherFor(java.toolchain).get().executablePath.asFile.absolutePath,
            files(tasks.jar.flatMap { it.archiveFile }, test.output).asPath)
    }
}
val bindingModelsTest by tasks.registering(Exec::class) {
    dependsOn(generateBindings)
    workingDir = file("..")
    doFirst {
        val runtime = providers.gradleProperty("jextract").orNull
            ?.let { file(it).parentFile.parentFile.resolve("runtime") }
            ?: generatedBindings.get().asFile.parentFile.resolve("toolchain/jextract-25/runtime")
        environment("OWH_JEXTRACT_RUNTIME", runtime.absolutePath)
        environment("OWH_KOTLIN_COMPILER_CLASSPATH", configurations.getByName("kotlinCompilerClasspath").asPath)
        commandLine(providers.gradleProperty("pythonExecutable").orElse("python").get(),
            "-m", "unittest", "discover", "-s", "tools/bindings/tests", "-v")
    }
}
tasks.check { dependsOn(artifactTest, nativeLoaderTest, overlayContracts, cacheTest, bindingModelsTest) }

publishing {
    publications {
        create<IvyPublication>("javaApi") { from(components["java"]) }
        create<IvyPublication>("kotlinApi") {
            module = "overwolf-hook-kotlin"
            artifact(kotlinApiJar)
            descriptor.withXml {
                val root = asNode()
                root.attributes()["xmlns:m"] = "http://ant.apache.org/ivy/maven"
                val configurations = (root["configurations"] as groovy.util.NodeList).first() as groovy.util.Node
                configurations.appendNode("conf", mapOf("name" to "default", "visibility" to "public"))
                val dependencies = (root["dependencies"] as groovy.util.NodeList).firstOrNull()
                    as? groovy.util.Node ?: root.appendNode("dependencies")
                val core = dependencies.appendNode("dependency", mapOf("org" to project.group,
                    "name" to project.name, "rev" to project.version, "conf" to "default->default"))
                core.appendNode("artifact", mapOf("name" to project.name, "type" to "jar",
                    "ext" to "jar", "m:classifier" to "windows-x64"))
                project.configurations.getByName(kotlinApi.implementationConfigurationName).allDependencies.forEach {
                    if (it.group != null && it.version != null) {
                        dependencies.appendNode("dependency", mapOf("org" to it.group, "name" to it.name,
                            "rev" to it.version, "conf" to "default->default"))
                    }
                }
            }
        }
    }
    repositories {
        ivy {
            name = "nativeStaging"
            url = layout.buildDirectory.dir("repository").get().asFile.toURI()
            patternLayout {
                artifact("v[revision]/[artifact]-[revision]-abi(-[classifier]).[ext]")
                ivy("v[revision]/[module]-[revision].ivy.xml")
            }
        }
        ivy {
            name = "kotlinStaging"
            url = layout.buildDirectory.dir("repository").get().asFile.toURI()
            patternLayout {
                artifact("v[revision]/[artifact]-[revision](-[classifier]).[ext]")
                ivy("v[revision]/[module]-[revision].ivy.xml")
            }
        }
    }
}
tasks.withType<PublishToIvyRepository>().configureEach {
    onlyIf { (publication.name == "javaApi") == (repository.name == "nativeStaging") }
}
tasks.withType<GenerateModuleMetadata>().configureEach { enabled = false }
