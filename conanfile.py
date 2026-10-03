"""Conan 2 recipe for legacy-cartographer's third-party dependencies.

Only libraries that Conan Center packages well are managed here. LLVM/Clang
(LibTooling) comes from the distribution's packages (llvm-18-dev,
libclang-18-dev, libclang-cpp18-dev): see docs/adr/0002-static-analysis-frontend.md.

Usage (one output folder per CMake preset, matching CMakePresets.json):
    scripts/conan-install.sh debug
"""

from conan import ConanFile


class LegacyCartographerConan(ConanFile):
    name = "legacy-cartographer"
    settings = "os", "arch", "compiler", "build_type"
    generators = "CMakeDeps", "CMakeToolchain"

    def requirements(self):
        # Pinned versions. TODO: verify availability in Conan Center on the
        # first CI run (could not be checked offline; docs/open-questions.md).
        self.requires("sqlite3/3.46.1")
        self.requires("nlohmann_json/3.11.3")

    def build_requirements(self):
        self.test_requires("gtest/1.15.0")
