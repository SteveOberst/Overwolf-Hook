pluginManagement { repositories { gradlePluginPortal(); mavenCentral() } }
dependencyResolutionManagement {
    repositories {
        exclusiveContent {
            forRepository {
                ivy {
                    name = "sdkUnderTest"
                    url = uri(providers.gradleProperty("sdkRepository").get())
                    patternLayout {
                        artifact("v[revision]/[artifact]-[revision]-abi(-[classifier]).[ext]")
                        ivy("v[revision]/[module]-[revision].ivy.xml")
                    }
                    metadataSources { ivyDescriptor(); artifact() }
                }
            }
            filter { includeGroup("dev.sxlver.native") }
        }
    }
}
rootProject.name = "owh-java-consumer-fixture"
