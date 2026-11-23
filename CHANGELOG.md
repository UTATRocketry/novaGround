# Changelog

What changed in this repo in each Nova release, newest first.

> [!NOTE]
> This file is **generated** by [git-cliff](https://git-cliff.org) from the PR
> titles merged into `dev`, by the `changelog` GitHub Actions workflow. Don't
> edit it by hand: change `cliff.toml` instead. Each release's test, notes and
> the other repos' versions are on the
> [Nova-Collected releases page](https://github.com/UTATRocketry/Nova-Collected/releases).

## [v2026.10.24](https://github.com/UTATRocketry/Nova-Collected/releases/tag/v2026.10.24) - 2026-10-24

### Fixes

- **pi:** run one novaGround unit per environment so the backend port follows the stack
- **install:** update library installation to include paho-mqttpp and use wildcard for WiringPi deb package
- **pi:** run one novaGround unit per environment so the backend port … (#14)
- update script permissions to make install, nova-pi, run, and start scripts executable
- ensure scripts are executable after checkout and update ExecStart for compatibility
- **scripts:** store executable bit on pi scripts (#15)

## [v2026.10.22](https://github.com/UTATRocketry/Nova-Collected/releases/tag/v2026.10.22) - 2026-10-21

### Documentation

- point contributors at dev clones

### Chores

- clean up README and add setup documentation for novaGround
- add missing chmod command for install script in README
- add changelog generation workflow and initial changelog files
- add changelog generation workflow and initial changelog files (#12)
- allow 'tests' prefix for branch names in PR checks
- allow 'tests' prefix for branch names in PR checks (#13)
- **github:** default code owner team and release review requests

## [v2026.10.11](https://github.com/UTATRocketry/Nova-Collected/releases/tag/v2026.10.11) - 2026-09-30

### Features

- **BREAKING** Add deploy functionality to nova-pi.sh for production releases

### Chores

- **BREAKING** Add CODEOWNERS, PR template, and CONTRIBUTING guidelines

