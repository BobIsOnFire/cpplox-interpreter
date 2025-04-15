from conan.tools.cmake import cmake_layout, CMakeToolchain, CMakeDeps
from conan import ConanFile

class HelloConan(ConanFile):
    settings = "os", "arch", "compiler", "build_type"

    options = {
        "shared": [True, False],
        "fPIC": [True, False],
        "sanitize": [None, "Address", "Leak", "Memory", "Thread", "Undefined"],
    }

    default_options = {
        "shared": False,
        "fPIC": True,
        "sanitize": None,
    }

    def layout(self):
        cmake_layout(self)

    def requirements(self):
        self.requires("fast_float/7.0.0")
        self.requires("magic_enum/0.9.7")
        self.requires("yaml-cpp/0.8.0")
        self.requires("gtest/1.12.1", test=True)
        pass

    def generate(self):
        tc = CMakeToolchain(self, generator="Ninja")
        tc.cache_variables["ENABLE_ADDRESS_SANITIZER"] = self.options.sanitize == "Address"
        tc.cache_variables["ENABLE_LEAK_SANITIZER"] = self.options.sanitize == "Leak"
        tc.cache_variables["ENABLE_MEMORY_SANITIZER"] = self.options.sanitize == "Memory"
        tc.cache_variables["ENABLE_THREAD_SANITIZER"] = self.options.sanitize == "Thread"
        tc.cache_variables["ENABLE_UNDEFINED_BEHAVIOUR_SANITIZER"] = self.options.sanitize == "Undefined"
        tc.generate()

        deps = CMakeDeps(self)
        deps.generate()
