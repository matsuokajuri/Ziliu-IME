# SPDX-License-Identifier: GPL-3.0-only
"""Fixed assets captured from official HTTPS, cross-checked against Git objects.

These are consistency pins, not author identity or corpus-rights authentication.
No weights, downloaded source bodies or private evidence are included.
"""
GIT_TREE_SHA1 = "4ded80050d424af3fb1e0c2c9700f7bfc68cc47e"
PINNED_SOURCE_FILES = {
    "BENCHMARK.md": {
        "bytes": 17314,
        "sha256": "3d9b09f284bbe9f195aa2f689a05aca3ddb990881ce598a26a9a665a2abcd4b5"
    },
    "BUILD.md": {
        "bytes": 5026,
        "sha256": "eb464d48ecf8079c1264f2008a71c348c9d7aeccc3581142dde6977a031f8258"
    },
    "LICENSE": {
        "bytes": 35149,
        "sha256": "3972dc9744f6499f0f9b2dbf76696f2ae7ad8af9b23dde66d6af86c9dfb36986"
    },
    "README.md": {
        "bytes": 29140,
        "sha256": "cd200223ffeb219468a6de82cff04802300a440b5038ee595e4f7349c0b051f5"
    },
    "THIRD_PARTY.md": {
        "bytes": 3366,
        "sha256": "20281c7f58be23758fb109df612145427f102ebafb44cfe38f7f794592d8517f"
    },
    "data/models/char_lm/char_lm.onnx.000": {
        "bytes": 52428800,
        "sha256": "3081b4308b1f63fd75ec294300609820b62bdb3251d4ddf4588591e969ec9100"
    },
    "data/models/char_lm/char_lm.onnx.001": {
        "bytes": 52428800,
        "sha256": "16d31013f761bcf327eb1ca62db0560439fbce1ba2ae25ba64cf991ff41cb96f"
    },
    "data/models/char_lm/char_lm.onnx.002": {
        "bytes": 52428800,
        "sha256": "ed7049e1e433189cbe089303aee0908b7111894f7625382a29e82eff00b49868"
    },
    "data/models/char_lm/char_lm.onnx.003": {
        "bytes": 19982874,
        "sha256": "b9c48ae6fe6d071f7009b467d908e394cdc60e1c9659da685eab70b134b1b887"
    },
    "data/models/char_lm/char_lm_vocab.bin": {
        "bytes": 48080,
        "sha256": "376c19b415024a2350083ca7c229bbe9f1d11191e562ef8ee174fd3afe7ec120"
    },
    "data/models/char_lm/pinyin_readings.json": {
        "bytes": 235074,
        "sha256": "77a7cb4bc2b4159d78af78d25032f30de0f22376f3dfb961c85fe16f05877433"
    },
    "data/models/char_lm/runtime_manifest.json": {
        "bytes": 1048,
        "sha256": "55893c141cee71ccba9142ba332594067295cb29b21582ba28c1883ce454a85b"
    },
    "src/engine/nc_char_lm.pas": {
        "bytes": 44931,
        "sha256": "80940e182b6c055d0ecaa598cbb05dda5f4db327a55179f21dc952efd6ec0dfa"
    },
    "src/host/native/nc_char_lm_ort.inc": {
        "bytes": 34590,
        "sha256": "8ec05efd98bc12cdc73de840162d693cb7832a8b5ab95b9f5e8e2d831e9815b9"
    },
    "src/host/native/nc_pinyin_transformer_ort.cpp": {
        "bytes": 2769,
        "sha256": "354ed8f0c29d55c974f65c508e1c8d6cb79cad2a84e2ef813572e2428e921d2c"
    },
    "src/host/nc_char_lm_host.pas": {
        "bytes": 19968,
        "sha256": "05c7a25016c608d98cacd86a336cb3f7726b57bad164386ac931ece07fa47d5a"
    },
    "third_party/onnxruntime/LICENSE": {
        "bytes": 1073,
        "sha256": "2f07c72751aed99790b8a4869cf2311df85a860b22ded05fa22803587a48922c"
    },
    "third_party/onnxruntime/ThirdPartyNotices.txt": {
        "bytes": 338538,
        "sha256": "cf7342f7ba482ef715ae58f5f497a8d3564fa255164175aea324cd293c5701a0"
    },
    "tools/build_pinyin_transformer_ort.ps1": {
        "bytes": 12675,
        "sha256": "0b0453057971745eeb4438a19cc15f3fa787f7ef790beb72766c4775e6b7a3fa"
    },
    "tools/char_lm_model_parts.ps1": {
        "bytes": 3505,
        "sha256": "0d49fc3f67a0cf14c97a8a3801927349cd8fbd55b8f5bedc6178416c37b56c0f"
    }
}
