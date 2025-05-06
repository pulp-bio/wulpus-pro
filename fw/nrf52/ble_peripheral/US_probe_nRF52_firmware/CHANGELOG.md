# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased] - 2025-05-01

### Added

- nRF52832 firmware from WULPUS repository version 1.2.2

### Fixed

### Changed
- Changed pin mapping of the SPI and supplementary (`HOST_READY`, `DATA_READY`) pins according to the schematics of the WULPUS PRO and connection to the nRF52 DK (see main README).
- Decreased the SPI frequency to 2 MHz for better signal integrity while testing with the wire jumpers interconnecting the PCBs.