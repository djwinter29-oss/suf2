# Releasing SUF2

This page covers what CI checks, how to cut a release, and the one-time
repository setup that publishing needs.

## What CI runs

Every pull request and every push to `main` runs **PR Check**
([pr-check.yml](../../.github/workflows/pr-check.yml)). It calls two
reusable workflows.

[Firmware validation](../../.github/workflows/firmware-validation.yml):

- Runs Ceedling unit tests (Unity/CMock), native verifier tests and the loader simulation
  (`tools/firmware/build.sh --test-only`).
- Uploads the unit-test reports, including JUnit XML.
- Builds the RP2040 and RP2350 loaders with throwaway test keys.
- Cross-builds the ESP32-S3 custom bootloader, USB recovery app and signed native
  blink example with the pinned ESP-IDF/TinyUSB libraries. Checks flash arguments,
  partition bounds, shared keys, native image metadata and signed BIN preservation.
- Runs `tools/release/verify-build.py` on each loader. It checks:
  - the loader fits in its flash region;
  - every RP2 loader UF2 block has the right family ID;
  - the signed blink example is accepted by the verification harness.
- Confirms that a production configure (`SUF2_PRODUCTION=ON`) refuses a
  test key.

[Host validation](../../.github/workflows/host-validation.yml):

- Dashboard syntax checks and tests, plus a dry run of the Pages staging.
- Python and .NET CLIs on Linux, macOS and Windows.
- Cross-producer checks on Linux and macOS, so Python, .NET and the dashboard
  must produce packages that verify the same way.
- Python tests on 3.10, the oldest supported version.
- A wheel built, installed into a clean virtual environment and used to pack
  an image.
- `bash -n` and ShellCheck on every script, and the tag-format rules.

You can run most of this locally with `tools/host/run_tests.sh`,
`tools/firmware/build.sh all --test` and `tools/validation/syntax-check-shell.sh`.

## Cutting a release

1. Merge everything into `main` and wait for PR Check to pass on that commit.
2. Tag the commit and push the tag:

   ```sh
   git tag v1.2.3
   git push origin v1.2.3
   ```

   Tags must be exactly `vMAJOR.MINOR.PATCH`: no leading zeros and no
   suffixes such as `-rc1`.

3. The **Release** workflow ([release.yml](../../.github/workflows/release.yml))
   runs both validations again and builds the release files. Before it
   publishes anything, it checks that PR Check passed on the tagged commit.
   A tag on a commit that PR Check never tested is refused.

The GitHub release contains:

| File | What it is |
| --- | --- |
| `suf2_host-1.2.3-py3-none-any.whl` | Python CLI (`pip install` it, then run `suf2`) |
| `suf2-dotnet-v1.2.3-linux-x64.tar.gz` | Self-contained .NET CLI; no .NET install needed |
| `suf2-dotnet-v1.2.3-osx-arm64.tar.gz` | The same for Apple silicon Macs |
| `suf2-dotnet-v1.2.3-win-x64.zip` | The same for Windows |
| `suf2-dashboard-v1.2.3.zip` | Offline copy of the dashboard; serve it from any static web server |
| `SHA256SUMS-*.txt` | SHA-256 checksums for each of the above |

When the release is published, two follow-up workflows start:

- [publish-pypi.yml](../../.github/workflows/publish-pypi.yml) downloads the
  wheel from the release, checks its checksum, name and version, and uploads
  it to PyPI as `suf2-host`.
- [publish-dashboard.yml](../../.github/workflows/publish-dashboard.yml)
  deploys the dashboard from the tagged commit to GitHub Pages.

Both re-check that the release is final and that its tests passed, so a
failed or draft release never reaches PyPI or Pages.

### Rehearsing a release

Run **Release** by hand from the Actions tab and give it a tag such as
`v1.2.3`. It builds and uploads every file as a workflow artifact, but it
creates no tag and no release. **Publish .NET CLI** can also be run on its
own the same way.

To try the Pages or PyPI step without a release, run the workflow by hand
with **test_publish** ticked. Be careful with PyPI: a test publish uploads a
`0.0.0.devN` version, and PyPI versions can never be reused or deleted.

## Why the release has no loader binaries

A SUF2 loader is built for one product. Its trusted public keys and its
product ID are compiled in. A loader built by CI would trust either a
throwaway test key or the repository's example key. Neither is safe to put
on a device.

So each product builds its own loader from the release tag:

```sh
tools/firmware/build.sh --no-sign rp2350 -- \
  -DSUF2_PRODUCTION=ON \
  -DSUF2_TRUSTED_KEYS=/path/to/your-public.pem \
  -DSUF2_PRODUCT_ID=<32 hex digits>
```

See [firmware/README.md](../../firmware/README.md) for all the build options.
The loaders CI builds for each pull request are kept for 14 days as
`suf2-dev-*` artifacts. They are for testing only.

## One-time repository setup

These settings are made by a maintainer. Nothing in the workflows changes
them.

**GitHub Pages.** Under *Settings → Pages*, set **Source** to
**GitHub Actions**. With the GitHub CLI:

```sh
gh api -X PUT repos/djwinter29-oss/suf2/pages -f build_type=workflow
```

Until you change this, Pages keeps serving the old `gh-pages` branch. Once
the switch is made and the first deployment succeeds, that branch can be
deleted.

**PyPI.** On PyPI, add a trusted publisher for the `suf2-host` project, or a
pending publisher if the project does not exist yet:

| Field | Value |
| --- | --- |
| Owner | `djwinter29-oss` |
| Repository | `suf2` |
| Workflow | `publish-pypi.yml` |
| Environment | `pypi` |

**Environments.** Create a `pypi` environment under *Settings → Environments*.
Adding required reviewers there gives you a manual approval step before every
upload. GitHub creates the `github-pages` environment itself.

**Branch protection.** Make the PR Check jobs required on `main`. The release
gate checks that PR Check passed, so it is only as strong as that rule.

## Dependency updates

Dependabot ([dependabot.yml](../../.github/dependabot.yml)) proposes weekly
updates for the Python package and the pinned GitHub Actions. All actions are
pinned to a full commit SHA, with the version in a comment.

The `pico-sdk`, Mbed TLS, ESP-IDF and standalone TinyUSB submodules are updated by hand. Move them to a
release tag, never a branch head, and note the change in the pull request.
