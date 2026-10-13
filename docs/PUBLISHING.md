# Public source boundary

[← README](../README.md) · [Ignore rules](../.gitignore) · [Notices](../THIRD_PARTY_NOTICES.md)

The initial GitHub publication was prepared from the current development workspace on **2026-10-13**. Source and documentation are published; raw local history stays on the development computer. The existing workspace, older packages and downloaded tools are preserved.

## What is included

| Material | Publication treatment |
| --- | --- |
| C application, COM camera source, microphone driver source, tests | Included; fake/synthetic cases and unfinished driver state are labeled |
| Managed scrcpy adapter and Android extension | Included with pinned build recipes and upstream notices |
| Build/run/test/publish scripts | Included; local dependency paths are examples or tool defaults |
| Phone Lens SVG/ICO/PNG and generated design boards | Included; illustrative boards are distinct from hardware evidence |
| README application image | Reviewed stopped-state screenshot, no selected phone, serial or private content |
| Performance plots, derived CSV and summary JSON | Included after review; numeric relative timings, no images/audio, source serial or absolute QPC/session identifiers |
| Test report | Public summaries and retained limitations; local evidence filenames refer to private development logs |

## What remains local

- `tools/`: downloaded upstream source trees, runtimes, Java/SDK/WDK inputs and archives.
- `build/`, `build-native/`, `build-driver/`: generated source, build caches, executables, DLLs, DEX assets and driver outputs.
- `evidence/`: raw ADB/Camera2/service dumps, process/session logs, trace files, UI captures, backups and historical ZIPs.
- `dist/`: timestamped development packages. Selected reviewed artifacts may be attached separately to a clearly labeled development release.
- User DPAPI settings, saved credentials, pairing codes, `.env` files, keys/certificates, dumps, PID files and Python/IDE caches.
- Git credentials remain in the system credential store; commit attribution uses the account's GitHub noreply address.

The ignore rules exclude entire local directories instead of trying to guess which raw logs are safe. The publication review checks the **actual staged Git files**, their types and sizes, suspicious credential patterns, local profile paths, network/identifier examples and all image assets. Public Markdown links and source paths are checked before upload.

### Initial review results

The staged source review covered **179 files**: 148 text files and 31 image assets, approximately 15.6 MB in total. No blocked/local directories, actual credentials, private-key/token patterns or developer-profile paths were found in the selected source. All **81 relative document/image links** resolved. SVGs had no scripts, embedded images or external references. Published CSVs contained relative numeric timing columns, and summaries omitted serial/session/absolute-QPC fields. All artwork, graphs and the real app screenshot were visually reviewed.

The initial runtime review found **31 embedded absolute developer-path strings** in `scrcpy-awm.exe`. Its CMake target now uses Clang's [file-prefix mapping](https://clang.llvm.org/docs/ClangCommandLineReference.html#cmdoption-clang-ffile-prefix-map) for source/log macros. The rebuilt client had **zero profile-path matches** and passed local version/help smoke checks without starting a phone session or changing ADB. All shipped owned executables and the camera DLL are reviewed again in the package.

The Windows publisher now copies the tracked source tree rather than recursively copying development directories. Generated Python bytecode and other untracked material stay local; unsigned kernel binaries are omitted. Review logs and image contact sheets stay under the excluded local `evidence/` directory. These checks are an explicit review of this snapshot, not a claim about future unreviewed changes.

## Release honesty

A Windows development ZIP is a separate artifact from source publication. It contains the owned user-mode executables, modified server, reviewed tracked source and notices. It omits unsigned kernel binaries, third-party runtime DLLs, untracked files, caches and unrelated/raw evidence. Driver source remains included. The runtime must be installed separately; the own microphone is still unavailable. Release notes and README must keep these facts explicit.

No project-wide license is selected implicitly by uploading the source. Existing component licenses and Apache-2.0 extension notices remain intact. [Review component versions and terms](../THIRD_PARTY_NOTICES.md).
