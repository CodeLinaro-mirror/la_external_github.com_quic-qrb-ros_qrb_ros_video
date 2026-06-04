## 0.2.0 (2026-06-04)

- BREAKING: rename `qrb_video_v4l2_lib` package to `qrb_video_lib`.
- BREAKING: rename shared library `libv4l2codecs.so` to `libqrb_video_codec.so`
  (CMake target `v4l2codecs` → `qrb_video_codec`); SOVERSION unchanged at 1.
- BREAKING: rename Debian packages `libqrb-video-v4l2-1` → `libqrb-video-codec-1`
  and `libqrb-video-v4l2-dev` → `libqrb-video-codec-dev`. Source package
  `qrb-video-v4l2-lib` → `qrb-video-lib`.
- The library now hosts two interchangeable codec backends — V4L2 (default)
  and VIDC (`-DENABLE_VIDC_BACKEND=ON`) — so the previous V4L2-specific
  naming has been retired.

## 0.1.7 (2025-09-25)

- docs: update usage  and installation instructions in README.md
- docs: update README.md with new template
- fix(mp4 container): video processing with framerate handling
- test: qrb_ros_video: Add GStreamer-based test nodes
- fix: qrb_ros_video: Improve code quality
- docs: add chapter on commit message format
- Added: enable  actionlin

## 0.1.0 (2025-01-16)

- Initial version release for Humble
- Contributors: Jean Xiao