# Local recovery capsules

`checkpoint_bundle.py` is a standard-library-only utility to create, verify, and
restore explicitly selected files. It does **not** upload, publish, install,
unpickle, import, or execute capsule contents. No real checkpoint or user data
is included here.

```sh
python checkpoint_bundle.py create backup.zip state/model.npz=checkpoint.npz
# Save the printed SHA-256 separately, in a durable trusted record.
python checkpoint_bundle.py verify backup.zip --sha256 <saved-sha256>
python checkpoint_bundle.py restore backup.zip fresh-directory --sha256 <saved-sha256>
python -m unittest discover -s tests -v
```

Creation hashes exactly the bytes archived, verifies the completed archive,
flushes it to disk, and uses a no-replace hard link to install the new backup.
Existing backups are never replaced. The destination filesystem must support
hard links and directory `fsync`; unsupported filesystems fail rather than
silently weakening the guarantee. This is tested on Linux, not Windows.

Restoration checks the external archive digest, every member, names, sizes, and
individual digests before creating a **new** output directory. Files are written
without replacement, read back, and a completion marker is written last. A
normal exception removes only the newly created directory. A killed process or
machine failure can leave an incomplete directory: never resume from a folder
without `RESTORE-COMPLETE.json`. Restoration is not an atomic directory rename
or a guarantee against every power-loss scenario. Keep previous verified
backups until a new backup has been downloaded and restored successfully.

The marker proves file restoration only. A model recovery drill must separately
check architecture/source versions, weights, optimizer tensors, RNG states,
step/cursor, frozen dependencies, data and partition identities, and perform a
bounded numerical test in a fresh environment. Include all of these dependencies
in an explicit inventory; a model-weight-only file cannot establish exact
training resumability. Do not silently reconstruct absent optimizer/RNG state.

Local hashes do not prove trusted provenance, durable off-machine backup,
training quality, Windows product acceptance, or distribution rights. Keep the
capsule and independent digest in authorized durable storage; verify readback.
Do not commit private capsules, weights, raw data, credentials, or machine paths
to a public repository. Public source synchronization and private checkpoint
backup are different operations. SHA-256 is not a signature.

The limits (10,000 payload members; 4 GiB expanded payload; 4 MiB manifest) bound capsule
inspection. The tool uses streaming content hashing rather than loading entire
model files into memory. Use a private directory not writable by untrusted
processes; this utility is not a sandbox against concurrent hostile filesystem
changes.

Select immutable checkpoint files from a completed step. Stop or coordinate the
checkpoint writer before creating the capsule: sequentially copying live model
and optimizer files cannot establish a consistent same-step snapshot. The
utility hashes bytes but does not infer cross-file semantic consistency.

Restored roots default to mode `0700`; payload files and the completion marker
default to `0600`, even with a permissive umask. These are Unix permission
semantics, not a claim about Windows ACL protection.
