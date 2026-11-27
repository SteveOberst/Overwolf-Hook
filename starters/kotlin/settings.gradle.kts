pluginManagement { repositories { gradlePluginPortal(); mavenCentral() } }

dependencyResolutionManagement {
    repositories {
        mavenCentral()
        exclusiveContent {
            forRepository {
                ivy {
                    name = "overwolfReleases"
                    url = uri(providers.gradleProperty("sdkRepository").orElse("https://github.com/SteveOberst/Overwolf-Hook/releases/download").get())
                    patternLayout {
                        artifact("v[revision]/[artifact]-[revision](-[classifier]).[ext]")
                        ivy("v[revision]/[module]-[revision].ivy.xml")
                    }
                    metadataSources { ivyDescriptor(); artifact() }
                }
            }
            filter { includeGroup("dev.sxlver.native") }
        }
    }
}
rootProject.name = "first-overlay-kotlin"
