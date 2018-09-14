import qbs

DynamicLibrary {
    name: "imx6camera"
    files: ["*.cpp", "*.h"]
    qbs.installPrefix: project.installPrefixIMX6Camera
    destinationDirectory: "IMX6Camera"
    Depends { name: "cpp" }
    Depends { name: "Qt"; submodules: ["core", "quick"] }

    cpp.dynamicLibraries: {
        var libs = [
            "v4l2",
        ];
        if (qbs.architecture.contains("arm"))
            libs.push("GAL", "GLESv2", "EGL")
        return libs;
    }

    Properties {
        condition: qbs.architecture.contains("arm")
        cpp.defines: [
            "ARM_TARGET",
        ]
    }

    Group {
        fileTagsFilter: "dynamiclibrary"
        qbs.install: true
        qbs.installDir: destinationDirectory
    }

    Group {
        name: "qmldir"
        qbs.install: true
        qbs.installDir: destinationDirectory
        files: "qmldir"
    }
}
