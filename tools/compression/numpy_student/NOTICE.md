# Source and licensing notes

These files are original Ziliu research implementations and synthetic fixtures, packaged under the repository's GNU GPL version3 license (the root license text is copied unchanged as `LICENSE`). This package contains no third-party model code, pretrained weights, vocabulary, corpus or NumPy binary distribution.

Mathematical documentation consulted by the original implementations is credited in their unchanged source headers:

- GRUCell reset-after-affine equations: https://docs.pytorch.org/docs/stable/generated/torch.nn.GRUCell.html
- AdamW equations: https://docs.pytorch.org/docs/stable/generated/torch.optim.AdamW.html

PyTorch is not a runtime dependency of these injected-NumPy implementations. NumPy, if separately obtained, retains its own license; it is not bundled. Citing an equation source is not a claim to PyTorch or model ownership, nor a grant to redistribute external model assets.
