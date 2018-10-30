#!/usr/bin/env python
# -*- coding: utf-8 -*-

from conans import CMake, ConanFile, tools
from os.path import exists


class ZWSSock(ConanFile):
    name = "zwssock"
    version = tools.load('version.txt').strip() if exists('version.txt') else ''
    url = "https://github.com/modbotrobotics/zwssock"
    description = "ZeroMQ over WebSocket Library"
    license = "MPL-2.0"

    build_policy = 'missing'
    default_options = "shared=False", "fPIC=True", "czmq:shared=True"
    exports = ["LICENSE.md"]
    exports_sources = "src/*", "test/*", "Makefile", "CMakeLists.txt", "version.txt"
    generators = ['cmake']
    options = {"shared": [True, False], "fPIC": [True, False]}
    settings = "os", "arch", "compiler", "build_type"
    requires = "czmq/4.1.0@modbot/stable"
  
    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()
        cmake.install()

    def package_info(self):
        self.cpp_info.libs = [self.name]