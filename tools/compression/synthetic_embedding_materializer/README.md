# Synthetic embedding materializer

This new standard-library-only API accepts **only small synthetic fixtures**, with at most 32 rows by 16 columns. It has no real-model namespace, model-derived lineage identifiers, actual vocabulary, weights, inputs, training labels, loader, tokenizer, training code or uploader. It is a separate implementation candidate and does not inherit approval or acceptance of any prior private implementation.

The API preserves finite FP32 values in packed little-endian FP64 matrices, including signed zero. Context UNK is preserved as its exact fixture row, whereas candidate-tail UNK is rejected without filtering. Every token must be a strict integer in range. Context BOS, complete four-pool ordering, duplicate aliases, input-token consistency and combined length bounds remain enforced. Separate pure helpers illustrate a conventional PAD/UNK/BOS plus character-row vocabulary; their configurable size does not enable real-model materialization.

Caller-provided descriptors and hashes establish fixture consistency only: they do not prove provenance or qualify private/real data for use. The API rejects any other namespace. Successful output remains pending independent materialization review and sets `student_update_authorized=false`.

Run `python3 -B -m unittest discover -s . -p 'test_*.py' -v` from this directory. The 32 synthetic tests were run locally and passed. No prior independent-check count is claimed for this new implementation; independent review is pending. These are source/fixture research checks, not model quality, actual data preparation, Windows product acceptance or permission to train.
