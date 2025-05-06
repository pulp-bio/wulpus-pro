# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased] - 2025-05-01

### Added

- GUI and library from WULPUS repository version 1.2.2
- A curve to the main GUI, visualizing the gain profile over time.

### Fixed

### Changed

- Extended the number of channels to 16.
- Modified TX/RX pin mapping.
- Added two new configuration parameters for VGA control:
    - `VGA Precharge time [cycles]`
    - `Wiper code for gain slope []`

- Extended the configuration package to accomodate two new parameters.

### Removed
- Removed `Capture restart time` and `Capture timeout time` from the old GUI.