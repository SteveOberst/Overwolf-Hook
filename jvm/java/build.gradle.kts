import groovy.json.JsonSlurper
import org.gradle.api.publish.ivy.IvyPublication

plugins { `java-library`; `ivy-publish` }
base { archivesName.set(rootProject.name) }
java { toolchain.languageVersion.set(JavaLanguageVersion.of(libs.versions.java.get().toInt())) }

val generatedBindings = layout.buildDirectory.dir("generated/sources/ffm")
val publicHeaderManifest = rootProject.file("../cmake/public-headers.txt")
val publicHeaders = publicHeaderManifest.readLines().filter { it.isNotBlank() }
    .map { rootProject.file("../include/overwolf_hook/$it") }
val toolchainPin = JsonSlurper().parse(rootProject.file("tools/jextract.json")) as Map<*, *>
val jextractHome = providers.gradleProperty("jextract")
    .map { rootProject.file(it).parentFile.parentFile }
    .orElse(generatedBindings.map {
        it.asFile.parentFile.resolve("toolchain/${toolchainPin["sha256"]}/${toolchainPin["directory"]}")
    })
val python = providers.gradleProperty("pythonExecutable").orElse("python")
val generateBindings by tasks.registering(Exec::class) {
    inputs.files(publicHeaderManifest, publicHeaders,
        rootProject.file("tools/generate_bindings.py"), rootProject.file("tools/jextract.json"),
        rootProject.fileTree("../tools/bindings") { include("**/*.py", "**/*.h") })
    inputs.property("jextract", providers.gradleProperty("jextract").orElse("pinned"))
    outputs.dir(generatedBindings)
    doFirst {
        val arguments = mutableListOf(python.get(), rootProject.file("tools/generate_bindings.py").absolutePath,
            "--output", generatedBindings.get().asFile.absolutePath)
        providers.gradleProperty("jextract").orNull?.let {
            arguments.addAll(listOf("--jextract", rootProject.file(it).absolutePath))
        }
        commandLine(arguments)
    }
}
sourceSets.main { java.srcDir(generateBindings) }

// CMake owns native compilation; the Java artifact embeds its matching DLL pair.
val nativeDirectory = providers.gradleProperty("nativeDirectory").map { rootProject.file(it) }
tasks.processResources {
    from(nativeDirectory) {
        include("owh_client.dll", "owh_agent.dll")
        into("overwolf/native/windows-x64")
    }
    from(listOf("LICENSE", "THIRD_PARTY_NOTICES.md", "third_party/MinHook.LICENSE.txt", "third_party/Boost.LICENSE.txt")
        .map { rootProject.file("../$it") }) { into("META-INF/overwolf-hook") }
    doFirst {
        val directory = nativeDirectory.orNull
            ?: throw GradleException("Set -PnativeDirectory to the matching CMake Client/Agent DLL directory")
        for (name in listOf("owh_client.dll", "owh_agent.dll"))
            if (!directory.resolve(name).isFile) throw GradleException("Missing native payload: ${directory.resolve(name)}")
    }
}
tasks.jar {
    archiveClassifier.set("windows-x64")
    manifest.attributes("Automatic-Module-Name" to "dev.sxlver.overwolfhook",
        "Implementation-Version" to version, "Overwolf-ABI-Version" to "1")
}
publishing.publications.create<IvyPublication>("javaApi") {
    module = base.archivesName.get()
    from(components["java"])
}

// These checks run the packaged JDK-only artifact, not the loose main classes.
// The Java test sources are main programs; Kotlin owns the JUnit tests.
tasks.test { enabled = false }
fun registerSmoke(name: String, entry: String) = tasks.register<JavaExec>(name) {
    dependsOn(tasks.testClasses, tasks.jar)
    classpath = files(tasks.jar.flatMap { it.archiveFile }, sourceSets.test.get().output)
    javaLauncher.set(javaToolchains.launcherFor(java.toolchain))
    mainClass.set(entry)
    jvmArgs("--enable-native-access=ALL-UNNAMED")
}
val artifactTest = registerSmoke("artifactTest", "JvmSmokeTest")
val nativeLoaderTest = registerSmoke("nativeLoaderTest", "NativeLoaderTest").apply {
    configure { args(layout.buildDirectory.dir("native-loader-test").get().asFile.absolutePath) }
}
val nativeLoaderBundledTest = registerSmoke("nativeLoaderBundledTest", "NativeLoaderTest").apply {
    configure { args(layout.buildDirectory.dir("native-loader-test").get().asFile.absolutePath, "bundled") }
}
val nativeLoaderFailureTest = registerSmoke("nativeLoaderFailureTest", "NativeLoaderTest").apply {
    configure { args(layout.buildDirectory.dir("native-loader-test").get().asFile.absolutePath, "failures") }
}
val overlayContracts = registerSmoke("overlayContracts", "dev.sxlver.overwolfhook.internal.OverlayContractTest")
val cacheTest by tasks.registering(Exec::class) {
    dependsOn(tasks.testClasses, tasks.jar)
    doFirst {
        commandLine(python.get(), rootProject.file("../tests/jvm_cache_test.py").absolutePath,
            javaToolchains.launcherFor(java.toolchain).get().executablePath.asFile.absolutePath,
            files(tasks.jar.flatMap { it.archiveFile }, sourceSets.test.get().output).asPath)
    }
}
val kotlinCompiler by configurations.creating
dependencies { kotlinCompiler(libs.kotlin.compiler) }
val bindingModelsTest by tasks.registering(Exec::class) {
    dependsOn(generateBindings)
    workingDir = rootProject.file("..")
    doFirst {
        environment("OWH_JEXTRACT_RUNTIME", jextractHome.get().resolve("runtime").absolutePath)
        environment("OWH_JAVA_EXECUTABLE", javaToolchains.launcherFor(java.toolchain).get().executablePath.asFile.absolutePath)
        environment("OWH_JAVAC_EXECUTABLE", javaToolchains.compilerFor(java.toolchain).get().executablePath.asFile.absolutePath)
        environment("OWH_KOTLIN_COMPILER_CLASSPATH", kotlinCompiler.asPath)
        commandLine(python.get(), "-m", "unittest", "discover", "-s", "tools/bindings/tests", "-v")
    }
}
tasks.check { dependsOn(artifactTest, nativeLoaderTest, nativeLoaderBundledTest,
    nativeLoaderFailureTest, overlayContracts, cacheTest, bindingModelsTest) }
