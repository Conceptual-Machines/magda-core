#!/usr/bin/env python3
"""
Windows Minidump (.dmp) parser for MAGDA crash analysis.
Extracts: exception info, crashing thread registers, module list, heuristic stack trace.
"""

import struct
import sys
import os

# Stream types
THREAD_LIST_STREAM      = 3
MODULE_LIST_STREAM      = 4
EXCEPTION_STREAM        = 6
SYSTEM_INFO_STREAM      = 7
MISC_INFO_STREAM        = 15

# Exception codes
EXCEPTION_CODES = {
    0xC0000005: "ACCESS_VIOLATION (null/dangling pointer)",
    0xC0000094: "INTEGER_DIVIDE_BY_ZERO",
    0xC0000096: "PRIVILEGED_INSTRUCTION",
    0xC000001D: "ILLEGAL_INSTRUCTION",
    0xC0000025: "NONCONTINUABLE_EXCEPTION",
    0xC0000026: "INVALID_DISPOSITION",
    0xC0000374: "HEAP_CORRUPTION",
    0x80000003: "BREAKPOINT",
    0x80000004: "SINGLE_STEP",
    0xC0000409: "STACK_BUFFER_OVERRUN",
}

ACCESS_VIOLATION_TYPE = {0: "read", 1: "write", 8: "DEP execute"}


def read_struct(data, offset, fmt):
    size = struct.calcsize(fmt)
    return struct.unpack_from(fmt, data, offset), offset + size


def read_minidump_string(data, rva):
    length = struct.unpack_from("<I", data, rva)[0]
    raw = data[rva + 4: rva + 4 + length]
    return raw.decode("utf-16-le", errors="replace")


def parse_header(data):
    sig = data[0:4]
    if sig != b"MDMP":
        raise ValueError(f"Not a minidump file (signature: {sig!r})")
    # MINIDUMP_HEADER: sig(4), version(4), num_streams(4), dir_rva(4), checksum(4), timestamp(4), flags(8)
    (version, num_streams, dir_rva, checksum, timestamp), _ = read_struct(data, 4, "<IIIII")
    flags = struct.unpack_from("<Q", data, 24)[0]
    return {
        "version": version,
        "num_streams": num_streams,
        "dir_rva": dir_rva,
        "timestamp": timestamp,
        "flags": flags,
    }


def parse_stream_directory(data, header):
    streams = {}
    offset = header["dir_rva"]
    for _ in range(header["num_streams"]):
        (stream_type, data_size, rva), offset = read_struct(data, offset, "<III")
        streams[stream_type] = {"size": data_size, "rva": rva}
    return streams


def parse_exception_stream(data, stream):
    off = stream["rva"]
    # MINIDUMP_EXCEPTION_STREAM: thread_id(4), alignment(4), then MINIDUMP_EXCEPTION
    (thread_id, _align), off = read_struct(data, off, "<II")

    # MINIDUMP_EXCEPTION:
    #   ExceptionCode(4), ExceptionFlags(4), ExceptionRecord(8), ExceptionAddress(8),
    #   NumberParameters(4), __unusedAlignment(4), ExceptionInformation[15](8*15=120)
    (exc_code, exc_flags), off = read_struct(data, off, "<II")
    exc_record_ptr = struct.unpack_from("<Q", data, off)[0]; off += 8
    exc_address   = struct.unpack_from("<Q", data, off)[0]; off += 8
    (num_params, _), off = read_struct(data, off, "<II")
    exc_info = list(struct.unpack_from("<15Q", data, off)); off += 120

    # MINIDUMP_LOCATION_DESCRIPTOR for thread context: DataSize(4), Rva(4)
    (ctx_size, ctx_rva), off = read_struct(data, off, "<II")

    return {
        "thread_id": thread_id,
        "exc_code": exc_code,
        "exc_flags": exc_flags,
        "exc_address": exc_address,
        "num_params": num_params,
        "exc_info": exc_info[:num_params],
        "ctx_size": ctx_size,
        "ctx_rva": ctx_rva,
    }


def parse_x64_context(data, rva, size):
    """Parse AMD64 CONTEXT structure, return dict of registers."""
    if size < 0x100:
        return {}
    # Offsets in AMD64 CONTEXT:
    # 0x030 ContextFlags, 0x034 MxCsr
    # 0x078 Rax, 0x080 Rcx, 0x088 Rdx, 0x090 Rbx, 0x098 Rsp, 0x0a0 Rbp
    # 0x0a8 Rsi, 0x0b0 Rdi, 0x0b8 R8..R15(8 regs), 0x0f8 Rip
    base = rva
    regs = {}
    names_offsets = [
        ("Rax", 0x078), ("Rcx", 0x080), ("Rdx", 0x088), ("Rbx", 0x090),
        ("Rsp", 0x098), ("Rbp", 0x0a0), ("Rsi", 0x0a8), ("Rdi", 0x0b0),
        ("R8",  0x0b8), ("R9",  0x0c0), ("R10", 0x0c8), ("R11", 0x0d0),
        ("R12", 0x0d8), ("R13", 0x0e0), ("R14", 0x0e8), ("R15", 0x0f0),
        ("Rip", 0x0f8),
    ]
    for name, off in names_offsets:
        if base + off + 8 <= len(data):
            regs[name] = struct.unpack_from("<Q", data, base + off)[0]
    return regs


def parse_module_list(data, stream):
    off = stream["rva"]
    (num_modules,), off = read_struct(data, off, "<I")
    modules = []
    # MINIDUMP_MODULE size = 108 bytes
    # BaseOfImage(8), SizeOfImage(4), CheckSum(4), TimeDateStamp(4), ModuleNameRva(4),
    # VS_FIXEDFILEINFO(52), CvRecord(8), MiscRecord(8), Reserved0(8), Reserved1(8)
    for _ in range(num_modules):
        base_addr = struct.unpack_from("<Q", data, off)[0]
        size      = struct.unpack_from("<I", data, off + 8)[0]
        timestamp = struct.unpack_from("<I", data, off + 16)[0]
        name_rva  = struct.unpack_from("<I", data, off + 20)[0]
        name = read_minidump_string(data, name_rva)
        modules.append({
            "base": base_addr,
            "size": size,
            "end":  base_addr + size,
            "name": os.path.basename(name),
            "path": name,
        })
        off += 108
    modules.sort(key=lambda m: m["base"])
    return modules


def parse_thread_list(data, stream):
    off = stream["rva"]
    (num_threads,), off = read_struct(data, off, "<I")
    threads = []
    # MINIDUMP_THREAD:
    #   ThreadId(4), SuspendCount(4), PriorityClass(4), Priority(4), Teb(8)
    #   Stack: StartOfMemoryRange(8), Memory.DataSize(4), Memory.Rva(4) = 16
    #   ThreadContext: DataSize(4), Rva(4) = 8
    for _ in range(num_threads):
        (tid, suspend, pclass, priority), off = read_struct(data, off, "<IIII")
        teb = struct.unpack_from("<Q", data, off)[0]; off += 8
        stack_start = struct.unpack_from("<Q", data, off)[0]; off += 8
        (stack_data_size, stack_rva), off = read_struct(data, off, "<II")
        (ctx_size, ctx_rva), off = read_struct(data, off, "<II")
        threads.append({
            "tid": tid,
            "suspend": suspend,
            "teb": teb,
            "stack_start": stack_start,
            "stack_data_size": stack_data_size,
            "stack_rva": stack_rva,
            "ctx_size": ctx_size,
            "ctx_rva": ctx_rva,
        })
    return threads


def addr_to_module(addr, modules):
    for m in modules:
        if m["base"] <= addr < m["end"]:
            return m["name"], addr - m["base"]
    return None, None


def scan_stack_for_addresses(data, thread, modules):
    """Heuristic: scan stack memory for 8-byte values that fall within known modules."""
    rva  = thread["stack_rva"]
    size = thread["stack_data_size"]
    if rva == 0 or size == 0:
        return []

    hits = []
    stack_bytes = data[rva: rva + size]
    for i in range(0, len(stack_bytes) - 8, 8):
        val = struct.unpack_from("<Q", stack_bytes, i)[0]
        mod_name, offset = addr_to_module(val, modules)
        if mod_name:
            hits.append((val, mod_name, offset))
    return hits


def parse_system_info(data, stream):
    off = stream["rva"]
    # ProcessorArchitecture(2), ProcessorLevel(2), ProcessorRevision(2), NumberOfProcessors(1), ProductType(1)
    (arch, level, rev, num_cpus, prod_type), off = read_struct(data, off, "<HHHBB")
    arch_names = {0: "x86", 5: "ARM", 6: "IA64", 9: "AMD64", 12: "ARM64"}
    return {
        "arch": arch_names.get(arch, f"unknown({arch})"),
        "num_cpus": num_cpus,
    }


def main(path):
    with open(path, "rb") as f:
        data = f.read()

    print(f"Minidump: {path}  ({len(data):,} bytes)\n")

    header  = parse_header(data)
    streams = parse_stream_directory(data, header)

    # System info
    if SYSTEM_INFO_STREAM in streams:
        si = parse_system_info(data, streams[SYSTEM_INFO_STREAM])
        print(f"Architecture: {si['arch']}  CPUs: {si['num_cpus']}")

    # Module list
    modules = []
    if MODULE_LIST_STREAM in streams:
        modules = parse_module_list(data, streams[MODULE_LIST_STREAM])

    # Exception
    print("\n" + "="*70)
    print("EXCEPTION")
    print("="*70)
    if EXCEPTION_STREAM not in streams:
        print("  No exception stream found.")
    else:
        exc = parse_exception_stream(data, streams[EXCEPTION_STREAM])
        code_desc = EXCEPTION_CODES.get(exc["exc_code"], f"0x{exc['exc_code']:08X}")
        print(f"  Thread ID:   0x{exc['thread_id']:X}")
        print(f"  Code:        0x{exc['exc_code']:08X}  {code_desc}")
        print(f"  Address:     0x{exc['exc_address']:016X}")

        mod_name, mod_off = addr_to_module(exc["exc_address"], modules)
        if mod_name:
            print(f"               → {mod_name} + 0x{mod_off:X}")

        if exc["exc_code"] == 0xC0000005 and exc["num_params"] >= 2:
            av_type = ACCESS_VIOLATION_TYPE.get(exc["exc_info"][0], f"type={exc['exc_info'][0]}")
            av_addr = exc["exc_info"][1]
            print(f"  AV type:     {av_type} at 0x{av_addr:016X}")
            if av_addr == 0:
                print("               → NULL pointer dereference")
            elif av_addr < 0x10000:
                print(f"               → Small offset from NULL (offset={av_addr}) — likely vtable/member of null object")

        # Crashing thread context (from exception stream)
        if exc["ctx_rva"] and exc["ctx_size"]:
            regs = parse_x64_context(data, exc["ctx_rva"], exc["ctx_size"])
            if regs:
                print("\n  Registers:")
                key_regs = ["Rip", "Rsp", "Rbp", "Rax", "Rcx", "Rdx", "Rbx",
                            "Rsi", "Rdi", "R8", "R9", "R10", "R11", "R12", "R13", "R14", "R15"]
                for name in key_regs:
                    if name in regs:
                        val = regs[name]
                        mn, mo = addr_to_module(val, modules)
                        suffix = f"  → {mn}+0x{mo:X}" if mn else ""
                        print(f"    {name:>4} = 0x{val:016X}{suffix}")

    # Thread list + stack scan
    print("\n" + "="*70)
    print("THREADS")
    print("="*70)
    threads = []
    if THREAD_LIST_STREAM in streams:
        threads = parse_thread_list(data, streams[THREAD_LIST_STREAM])
        crashing_tid = exc["thread_id"] if EXCEPTION_STREAM in streams else None

        for t in threads:
            marker = " *** CRASHING ***" if t["tid"] == crashing_tid else ""
            regs = parse_x64_context(data, t["ctx_rva"], t["ctx_size"]) if t["ctx_rva"] else {}
            rip = regs.get("Rip", 0)
            mn, mo = addr_to_module(rip, modules)
            rip_str = f"  RIP=0x{rip:016X}" + (f" → {mn}+0x{mo:X}" if mn else "")
            print(f"\n  TID=0x{t['tid']:X}{marker}{rip_str}")

    # Stack trace for crashing thread
    print("\n" + "="*70)
    print("STACK SCAN (crashing thread)")
    print("="*70)
    if EXCEPTION_STREAM in streams and threads:
        crashing_tid = exc["thread_id"]
        ct = next((t for t in threads if t["tid"] == crashing_tid), None)
        if ct:
            hits = scan_stack_for_addresses(data, ct, modules)
            # Deduplicate consecutive same-module hits
            seen = set()
            count = 0
            for addr, mod_name, offset in hits:
                key = (mod_name, offset)
                if key in seen:
                    continue
                seen.add(key)
                print(f"  0x{addr:016X}  {mod_name}+0x{offset:X}")
                count += 1
                if count >= 60:
                    print(f"  ... ({len(hits) - count} more)")
                    break
        else:
            print("  Could not find crashing thread in thread list.")

    # Module list
    print("\n" + "="*70)
    print("MODULES")
    print("="*70)
    for m in modules:
        print(f"  0x{m['base']:016X} - 0x{m['end']:016X}  {m['name']}")

    print()


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <file.dmp>")
        sys.exit(1)
    main(sys.argv[1])
