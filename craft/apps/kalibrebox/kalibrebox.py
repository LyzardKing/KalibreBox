import info
from CraftCore import CraftCore
from Package.CMakePackageBase import CMakePackageBase


class subinfo(info.infoclass):
    def setTargets(self):
        self.displayName = "KalibreBox"
        self.description = "Calibre e-book library manager for Dropbox, built with Kirigami"
        self.webpage = "https://github.com/galileo/KalibreBox"

        # Local development is the default flow:
        #   craft --options apps/kalibrebox.srcDir=<repo-path> apps/kalibrebox --package
        self.svnTargets["master"] = ""
        self.defaultTarget = "master"
        # Once a public git URL exists, switch to:
        #   self.versionInfo.setDefaultValues(gitUrl="https://invent.kde.org/.../kalibrebox.git")

    def setDependencies(self):
        self.buildDependencies["virtual/base"] = None
        self.runtimeDependencies["libs/qt/qtbase"] = None
        self.runtimeDependencies["libs/qt/qtdeclarative"] = None
        self.runtimeDependencies["libs/qt/qtsvg"] = None
        self.runtimeDependencies["kde/frameworks/tier1/kirigami"] = None
        self.runtimeDependencies["kde/frameworks/tier1/ki18n"] = None

        if not CraftCore.compiler.platform.isAndroid:
            self.runtimeDependencies["kde/frameworks/tier3/qqc2-desktop-style"] = None
        else:
            self.runtimeDependencies["kde/plasma/qqc2-breeze-style"] = None
            self.runtimeDependencies["kde/frameworks/tier1/breeze-icons"] = None

    def registerOptions(self):
        # The repo builds tests with plain CMake; craft passes only what it needs.
        self.options.configure.args += ["-DQT_MAJOR_VERSION=6"]


class Package(CMakePackageBase):
    def __init__(self, **kwargs):
        super().__init__(**kwargs)

    def createPackage(self):
        if not CraftCore.compiler.platform.isAndroid and CraftCore.compiler.isWindows:
            self.defines["executable"] = r"bin\kalibrebox.exe"
        return super().createPackage()
