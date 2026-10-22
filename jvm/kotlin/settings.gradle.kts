pluginManagement { repositories { gradlePluginPortal(); mavenCentral() } }
dependencyResolutionManagement {
    repositories {
        mavenCentral()
        exclusiveContent {
            forRepository {
                ivy {
                    url = uri("https://github.com/SteveOberst/Overwolf-Hook/releases/download")
                    patternLayout {
                        artifact("v[revision]/[artifact]-[revision](-[classifier]).[ext]")
                        ivy("v[revision]/[module]-[revision].ivy.xml")
                    }
                    metadataSources { ivyDescriptor(); artifact() }
                }
            }
            filter { includeModule("dev.sxlver.native", "overwolf-hook") }
        }
    }
}
rootProject.name = "overwolf-hook-kotlin"
