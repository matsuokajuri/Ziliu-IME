"""Worker-side checks only. The reviewed controller owns launch/watchdog/cleanup."""
from datetime import datetime, timezone
import hashlib
import importlib.metadata
import os
from pathlib import Path
import sys
import time

from .contracts import exact, hash_value
from .training_data import read_pinned, parse_json


def windows_limits(memory_bytes, controller_pid, source_job_handle):
    """Duplicate QUERY-only access to the exact controller-owned Job, then close it."""
    import ctypes as c
    from ctypes import wintypes as w
    class Basic(c.Structure):
        _fields_ = [("user", c.c_int64), ("job_user", c.c_int64), ("flags", w.DWORD),
                    ("min_ws", c.c_size_t), ("max_ws", c.c_size_t), ("active_limit", w.DWORD),
                    ("affinity", c.c_size_t), ("priority", w.DWORD), ("scheduling", w.DWORD)]
    class IO(c.Structure):
        _fields_ = [(f"v{i}", c.c_uint64) for i in range(6)]
    class Extended(c.Structure):
        _fields_ = [("basic", Basic), ("io", IO), ("process_memory", c.c_size_t),
                    ("job_memory", c.c_size_t), ("peak_process", c.c_size_t), ("peak_job", c.c_size_t)]
    kernel = c.WinDLL("kernel32", use_last_error=True)
    for name, args, result in (
        ("GetCurrentProcess", [], w.HANDLE),
        ("OpenProcess", [w.DWORD, w.BOOL, w.DWORD], w.HANDLE),
        ("DuplicateHandle", [w.HANDLE, w.HANDLE, w.HANDLE, c.POINTER(w.HANDLE), w.DWORD, w.BOOL, w.DWORD], w.BOOL),
        ("CloseHandle", [w.HANDLE], w.BOOL),
        ("IsProcessInJob", [w.HANDLE, w.HANDLE, c.POINTER(w.BOOL)], w.BOOL),
        ("QueryInformationJobObject", [w.HANDLE, c.c_int, c.c_void_p, w.DWORD, c.c_void_p], w.BOOL),
        ("GetPriorityClass", [w.HANDLE], w.DWORD),
        ("GetProcessAffinityMask", [w.HANDLE, c.POINTER(c.c_size_t), c.POINTER(c.c_size_t)], w.BOOL)):
        fn = getattr(kernel, name); fn.argtypes = args; fn.restype = result
    def checked(ok):
        if not ok: raise c.WinError(c.get_last_error())
    handle = kernel.GetCurrentProcess()
    if type(controller_pid) is not int or controller_pid <= 0 or source_job_handle <= 0:
        raise PermissionError("explicit controller/Job handle identity required")
    controller = kernel.OpenProcess(0x40, False, controller_pid)  # PROCESS_DUP_HANDLE, owned launcher only.
    checked(controller)
    job = w.HANDLE()
    try:
        checked(kernel.DuplicateHandle(controller, w.HANDLE(source_job_handle), handle, c.byref(job),
                                       0x4, False, 0))  # JOB_OBJECT_QUERY only, non-inheritable.
    finally:
        kernel.CloseHandle(controller)
    limits = Extended()
    try:
        in_job = w.BOOL()
        checked(kernel.IsProcessInJob(handle, job, c.byref(in_job)))
        if not in_job.value: raise PermissionError("worker is outside the exact owned Job")
        checked(kernel.QueryInformationJobObject(job, 9, c.byref(limits), c.sizeof(limits), None))
    finally:
        kernel.CloseHandle(job)
    affinity, system = c.c_size_t(), c.c_size_t()
    checked(kernel.GetProcessAffinityMask(handle, c.byref(affinity), c.byref(system)))
    required = 0x2000 | 0x200 | 0x10 | 0x20  # kill-on-close, Job memory, affinity, priority
    if (limits.basic.flags & required != required or limits.basic.flags & (0x800 | 0x1000)
            or limits.job_memory != memory_bytes or limits.basic.affinity != 3
            or limits.basic.priority != 0x4000 or affinity.value != 3
            or kernel.GetPriorityClass(handle) != 0x4000):
        raise PermissionError("CPU2/frozen-memory/below-normal/no-breakaway Job limits differ")


def authorize(permit_path, permit_sha256, input_sha256):
    # Environment is populated only by a separately authorized, reviewed launcher.
    # These checks detect accidental bypass; local files are not cryptographic authority.
    if os.environ.get("ZILIU_STUDENT_TRAIN_PERMIT_SHA256") != permit_sha256 or not permit_sha256:
        raise PermissionError("no separately authorized training permit")
    if os.name != "nt" or os.environ.get("CUDA_VISIBLE_DEVICES") != "-1":
        raise PermissionError("owned Windows CPU-only training window required")
    permit = read_pinned(permit_path, permit_sha256, 64*1024)
    exact(permit, {"schema", "purpose", "input_manifest_sha256", "python", "python_sha256",
                   "torch_version", "numpy_version", "source_pins", "lease_token_sha256"})
    if (permit["schema"] != "ziliu.ranking-training-permit.v1"
            or permit["purpose"] not in {"optimizer_training", "synthetic_pipeline_qualification"}
            or permit["input_manifest_sha256"] != input_sha256):
        raise PermissionError("explicit training or bounded synthetic pipeline permit required")
    synthetic = permit["purpose"] == "synthetic_pipeline_qualification"
    memory_bytes = (4 if synthetic else 2)*1024**3
    owner = "ranking-student-pipeline-qualification" if synthetic else "ranking-student-training"
    if str(Path(sys.executable).resolve()) != permit["python"]:
        raise PermissionError("frozen interpreter differs")
    if hashlib.sha256(Path(sys.executable).read_bytes()).hexdigest() != permit["python_sha256"]:
        raise PermissionError("interpreter byte pin differs")
    for package in ("torch", "numpy"):
        if importlib.metadata.version(package) != permit[package+"_version"]:
            raise PermissionError("installed runtime version differs")
    # Pin every first-party module used by this worker, including the gate helpers.
    root = Path(__file__).resolve().parents[3]
    expected_paths = {str(p.relative_to(root)).replace("\\", "/")
                      for p in Path(__file__).parent.glob("*.py")}
    expected_paths.update("scripts/"+n for n in ("offline_evaluation_contracts.py", "offline_pool_evaluation.py",
        "offline_context_policy_v2.py", "offline_rerank_protocol.py", "offline_candidate_evidence.py"))
    if type(permit["source_pins"]) is not dict or set(permit["source_pins"]) != expected_paths:
        raise PermissionError("complete worker/gate source pins required")
    for relative, sha in permit["source_pins"].items():
        hash_value(sha)
        if hashlib.sha256((root/relative).read_bytes()).hexdigest() != sha:
            raise PermissionError("frozen source byte pin differs")
    lease_path = Path(os.environ["LOCALAPPDATA"])/"Temp"/"Ziliu-heavy-workload.lock"
    def read_lease():
        with lease_path.open("rb") as stream: raw = stream.read(16*1024+1)
        if len(raw) > 16*1024: raise PermissionError("oversize lease")
        return parse_json(raw)
    lease = read_lease()
    token = os.environ.get("ZILIU_STUDENT_TRAIN_TOKEN", "")
    if (not token or lease.get("owner") != owner or lease.get("token") != token
            or hashlib.sha256(token.encode()).hexdigest() != permit["lease_token_sha256"]
            or lease.get("cpu_threads") != 2 or lease.get("job_memory_bytes") != memory_bytes):
        raise PermissionError("training-only live lease differs")
    start, end = (datetime.fromisoformat(lease[k]) for k in ("created_utc", "expires_utc"))
    now = datetime.now(timezone.utc)
    if not 0 < (end-start).total_seconds() <= 300 or not start <= now < end:
        raise PermissionError("training lease expired or exceeds 300 seconds")
    deadline = time.monotonic() + (end-now).total_seconds() - 30
    windows_limits(memory_bytes, lease.get("pid"), int(os.environ.get("ZILIU_STUDENT_JOB_QUERY_HANDLE", "0")))
    def check():
        if time.monotonic() >= deadline or read_lease() != lease:
            raise TimeoutError("training work budget/lease ended; reserve cleanup")
    check()
    return check, permit
