# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](http://keepachangelog.com/en/1.0.0/)
and this project adheres to [Semantic Versioning](http://semver.org/spec/v2.0.0.html).


## [1.0.1] - 2018-12-13

### Added

- Added websocket close packet code and reason parsing

### Fixed

- Fixed clients not being destroyed on empty (close) frame received by router; clients are now marked as in exception state and deleted
- Fixed client / server compression factor and permessage deflate value parsing


## [1.0.0] - 2018-11-01

### Added

- Forked repository from [zeromq/zwssock](https://github.com/zeromq/zwssock)
- Added conan recipe `conanfile.py`; includes `czmq` conan package dependency
- Added `CMakeLists.txt`
- Added `Makefile`

### Changed

- Refactored directory structure
- Updated `README.md`
