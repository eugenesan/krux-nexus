#!/usr/bin/env python3
"""Parse the kernel's per-frame CPP config dumps.

Each /data/local/tmp/cpp_frm_NNN.bin contains:

    struct msm_cpp_frame_info_t   (32-bit ARM layout)
    uint32_t cpp_cmd_msg[msg_len]

Produced by the cpp_dump_frame() hook in
drivers/media/platform/msm/camera_v2/pproc/cpp/msm_cpp.c.
"""

import ctypes
import struct
import sys


class CppBufferInfo(ctypes.Structure):
    _fields_ = [
        ("fd", ctypes.c_int32),
        ("index", ctypes.c_uint32),
        ("offset", ctypes.c_uint32),
        ("native_buff", ctypes.c_uint8),
        ("processed_divert", ctypes.c_uint8),
        ("_pad", ctypes.c_uint8 * 2),
    ]


class CppFrameInfo(ctypes.Structure):
    _fields_ = [
        ("frame_id", ctypes.c_int32),
        ("timestamp_sec", ctypes.c_int32),
        ("timestamp_usec", ctypes.c_int32),
        ("inst_id", ctypes.c_uint32),
        ("identity", ctypes.c_uint32),
        ("client_id", ctypes.c_uint32),
        ("frame_type", ctypes.c_int32),
        ("num_strips", ctypes.c_uint32),
        ("strip_info", ctypes.c_uint32),
        ("msg_len", ctypes.c_uint32),
        ("cpp_cmd_msg", ctypes.c_uint32),
        ("src_fd", ctypes.c_int32),
        ("dst_fd", ctypes.c_int32),
        ("src_ion_handle", ctypes.c_uint32),
        ("dest_ion_handle", ctypes.c_uint32),
        ("in_timestamp_sec", ctypes.c_int32),
        ("in_timestamp_usec", ctypes.c_int32),
        ("out_timestamp_sec", ctypes.c_int32),
        ("out_timestamp_usec", ctypes.c_int32),
        ("cookie", ctypes.c_uint32),
        ("status", ctypes.c_uint32),
        ("duplicate_output", ctypes.c_int32),
        ("duplicate_identity", ctypes.c_uint32),
        ("input", CppBufferInfo),
        ("output0", CppBufferInfo),
        ("output1", CppBufferInfo),
    ]


def load(path):
    data = open(path, "rb").read()
    if len(data) < ctypes.sizeof(CppFrameInfo):
        raise SystemExit("%s: too short (%d bytes)" % (path, len(data)))
    info = CppFrameInfo.from_buffer_copy(data)
    words = list(
        struct.unpack(
            "<%dI" % info.msg_len,
            data[ctypes.sizeof(CppFrameInfo) : ctypes.sizeof(CppFrameInfo) + 4 * info.msg_len],
        )
    )
    return info, words


def show(path):
    info, words = load(path)
    print(path)
    print(
        "  id=0x%x type=%d strips=%u words=%u duplicate=%d dup_id=0x%x"
        % (
            info.identity,
            info.frame_type,
            info.num_strips,
            info.msg_len,
            info.duplicate_output,
            info.duplicate_identity,
        )
    )
    for name, b in (
        ("in ", info.input),
        ("out0", info.output0),
        ("out1", info.output1),
    ):
        print(
            "  %s fd=%d index=%u offset=%u native=%u divert=%u"
            % (name, b.fd, b.index, b.offset, b.native_buff, b.processed_divert)
        )
    if words:
        print("  words[0:8]  = %s" % " ".join("%08x" % w for w in words[:8]))
        print("  words[-4:]  = %s" % " ".join("%08x" % w for w in words[-4:]))


def summary(paths):
    print("%-28s %-10s %-16s %-8s %-8s %s"
          % ("file", "identity", "input", "out0", "strips", "words"))
    for path in paths:
        info, words = load(path)
        print("%-28s 0x%-8x idx=%-2u fd=%-3d  idx=%-2u   %-6u %u"
              % (path.rsplit("/", 1)[-1], info.identity,
                 info.input.index, info.input.fd,
                 info.output0.index, info.num_strips, info.msg_len))


def diff(paths):
    """Report struct fields and message words that change between frames."""
    frames = [load(p) for p in paths]
    if len(frames) < 2:
        raise SystemExit("need at least two dumps to diff")

    base, base_words = frames[0]
    print("frames: %d" % len(frames))
    print("identity: %s" % sorted({hex(f[0].identity) for f in frames}))
    for field in (
        "frame_id",
        "inst_id",
        "client_id",
        "frame_type",
        "num_strips",
        "msg_len",
        "duplicate_output",
        "duplicate_identity",
    ):
        vals = sorted({getattr(f[0], field) for f in frames})
        print("%-18s %s" % (field, vals if len(vals) < 8 else len(vals)))

    for name in ("input", "output0", "output1"):
        print("%-18s" % name)
        for attr in ("fd", "index", "offset", "native_buff", "processed_divert"):
            vals = sorted({getattr(getattr(f[0], name), attr) for f in frames})
            print("  %-16s %s" % (attr, vals if len(vals) < 12 else "%d values" % len(vals)))

    n = min(len(w) for _, w in frames)
    changing = [i for i in range(n) if len({w[i] for _, w in frames}) > 1]
    print("message words: %d total, %d change across frames" % (n, len(changing)))
    for i in changing[:24]:
        vals = [w[i] for _, w in frames[:6]]
        print("  word[%3d] %s" % (i, " ".join("%08x" % v for v in vals)))


if __name__ == "__main__":
    args = sys.argv[1:]
    if args and args[0] == "--summary":
        summary(args[1:])
    elif args and args[0] == "--diff":
        diff(args[1:])
    else:
        for arg in args:
            show(arg)
