# Third-party attribution

The offline tree scorer is a modified Python adaptation of the character LM
scoring interface and algorithm in `shenmin/cassotis-ime`, commit
`e4d632d20c296fc5f3dd1bfe3c74be6b60cafca2`,
`src/host/native/nc_char_lm_ort.inc`. Cassotis IME is licensed under GPL-3.0.
The modification date is 2026-10-06. The GPL text is in the repository root [LICENSE](../../LICENSE).

Reference sources:

- https://github.com/shenmin/cassotis-ime/blob/e4d632d20c296fc5f3dd1bfe3c74be6b60cafca2/src/host/native/nc_char_lm_ort.inc
- https://github.com/shenmin/cassotis-ime/blob/e4d632d20c296fc5f3dd1bfe3c74be6b60cafca2/THIRD_PARTY.md
- https://github.com/matsuokajuri/Ziliu-IME/blob/f7231c96e6fb5982638871cc0b31ebacb5d0932a/LICENSE

Runtime dependency: Microsoft ONNX Runtime 1.20.1 CPU, MIT. A separately prepared
official wheel must retain `onnxruntime/LICENSE` and
`onnxruntime/ThirdPartyNotices.txt`. NumPy is a separate runtime dependency under
its bundled BSD and third-party notices. No runtime binary is included here.

The Cassotis model files and vocabulary are separately acquired inference
assets. The project states that it trained the model on separately licensed
corpora but does not enumerate those corpora or their permissions in the pinned
notice. Its root GPL notice does not by itself establish the entire training
or model redistribution rights chain. No model weight, corpus, checkpoint,
wheel, personal input or private experiment output is distributed by this tree.
