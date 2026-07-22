rootProject.name = "grpc-libreoffice"

include("grlibre-api")
include("grlibre-service")

dependencyResolutionManagement {
    repositories {
        mavenCentral()
    }
}
