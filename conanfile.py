from conan import ConanFile
from conan.tools.cmake import cmake_layout

class NetworkedPhysicsRecipe(ConanFile):
    settings = "os", "compiler", "build_type", "arch"
    generators = "CMakeDeps", "CMakeToolchain"

    def requirements(self):
        self.requires("joltphysics/5.2.0")
        self.requires("glew/2.2.0")
        self.requires("libpng/1.6.44")
        self.requires("glfw/3.4")
        self.requires("glm/1.0.1")

    def configure(self):
        # Configure JoltPhysics options
        self.options["joltphysics"].shared = False
        self.options["joltphysics"].double_precision = False
        self.options["joltphysics"].cross_platform_deterministic = False
        self.options["joltphysics"].enable_asserts = True

    def layout(self):
        cmake_layout(self)
