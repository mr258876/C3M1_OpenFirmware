# Development archive

This branch preserves the v0.2.21 pre-consolidation source, experimental build flags and host fixtures. Main is the clean production tree.

```sh
bash build_v221.sh
bash tests/run_v221_tests.sh
```

The historical test scripts correspond to the older build matrix. Some require their original external RAW calibration samples or earlier firmware images; those are not tracked here. The latest UI, Flash, playback and post-shot regression suite ran successfully in the release-preparation environment. Use this branch as an experimental archive rather than the public distribution.

CI builds this archive using `C3_BUILD_TAG=CI bash build.sh`. Download the artifact from the Actions run; use main for the current release.
