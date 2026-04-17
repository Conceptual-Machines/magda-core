#!/usr/bin/env python3
"""
Windows Minidump (.dmp) parser for MAGDA crash analysis.

Usage:
  parse-minidump.py <file.dmp>
  parse-minidump.py <file.dmp> --pdb <MAGDA.pdb>

With --pdb: stack addresses are resolved to function names using
llvm-pdbutil (installed at /opt/homebrew/opt/llvm/bin/ on macOS).
"""

import struct
import sys
import os
import subprocess

# ── Stream types ─────────────────────────────────────────────────────────────
THREAD_LIST_STREAM  = 3
MODULE_LIST_STREAM  = 4
EXCEPTION_STREAM    = 6
SYSTEM_INFO_STREAM  = 7

# ── Exception codes ───────────────────────────────────────────────────────────
EXCEPTION_CODES = {
    0xC0000005: "ACCESS_VIOLATION (null/dangling pointer)",
    0xC0000094: "INTEGER_DIVIDE_BY_ZERO",
    0xC0000096: "PRIVILEGED_INSTRUCTION",
    0xC000001D: "ILLEGAL_INSTRUCTION",
    0xC0000374: "HEAP_CORRUPTION",
    0x80000003: "BREAKPOINT",
    0xC0000409: "STACK_BUFFER_OVERRUN",
}
ACCESS_VIOLATION_TYPE = {0: "read", 1: "write", 8: "DEP execute"}

# ── Binary helpers ────────────────────────────────────────────────────────────

def read_struct(data, offset, fmt):
    size = struct.calcsize(fmt)
    return struct.unpack_from(fmt, data, offset), offset + size

def read_minidump_string(data, rva):
    length = struct.unpack_from("<I", data, rva)[0]
    raw = data[rva + 4: rva + 4 + length]
    return raw.decode("utf-16-le", errors="replace")

# ── Stream parsers ────────────────────────────────────────────────────────────

def parse_header(data):
    if data[0:4] != b"MDMP":
        raise ValueError(f"Not a minidump (signature: {data[0:4]!r})")
    (version, num_streams, dir_rva, checksum, timestamp), _ = read_struct(data, 4, "<IIIII")
    return {"num_streams": num_streams, "dir_rva": dir_rva}

def parse_stream_directory(data, header):
    streams = {}
    offset = header["dir_rva"]
    for _ in range(header["num_streams"]):
        (stream_type, data_size, rva), offset = read_struct(data, offset, "<III")
        streams[stream_type] = {"size": data_size, "rva": rva}
    return streams

def parse_system_info(data, stream):
    off = stream["rva"]
    (arch, level, rev, num_cpus, prod_type), _ = read_struct(data, off, "<HHHBB")
    arch_names = {0: "x86", 5: "ARM", 6: "IA64", 9: "AMD64", 12: "ARM64"}
    return {"arch": arch_names.get(arch, f"unknown({arch})"), "num_cpus": num_cpus}

def parse_exception_stream(data, stream):
    off = stream["rva"]
    (thread_id, _align), off = read_struct(data, off, "<II")
    (exc_code, exc_flags), off = read_struct(data, off, "<II")
    off += 8  # ExceptionRecord ptr
    exc_address = struct.unpack_from("<Q", data, off)[0]; off += 8
    (num_params, _), off = read_struct(data, off, "<II")
    exc_info = list(struct.unpack_from("<15Q", data, off)); off += 120
    (ctx_size, ctx_rva), _ = read_struct(data, off, "<II")
    return {
        "thread_id": thread_id, "exc_code": exc_code,
        "exc_address": exc_address, "num_params": num_params,
        "exc_info": exc_info[:num_params], "ctx_size": ctx_size, "ctx_rva": ctx_rva,
    }

def parse_x64_context(data, rva, size):
    if size < 0x100:
        return {}
    base = rva
    names_offsets = [
        ("Rax", 0x078), ("Rcx", 0x080), ("Rdx", 0x088), ("Rbx", 0x090),
        ("Rsp", 0x098), ("Rbp", 0x0a0), ("Rsi", 0x0a8), ("Rdi", 0x0b0),
        ("R8",  0x0b8), ("R9",  0x0c0), ("R10", 0x0c8), ("R11", 0x0d0),
        ("R12", 0x0d8), ("R13", 0x0e0), ("R14", 0x0e8), ("R15", 0x0f0),
        ("Rip", 0x0f8),
    ]
    regs = {}
    for name, off in names_offsets:
        if base + off + 8 <= len(data):
            regs[name] = struct.unpack_from("<Q", data, base + off)[0]
    return regs

def parse_module_list(data, stream):
    off = stream["rva"]
    (num_modules,), off = read_struct(data, off, "<I")
    modules = []
    for _ in range(num_modules):
        base_addr = struct.unpack_from("<Q", data, off)[0]
        size      = struct.unpack_from("<I", data, off + 8)[0]
        name_rva  = struct.unpack_from("<I", data, off + 20)[0]
        name = read_minidump_string(data, name_rva)
        modules.append({
            "base": base_addr, "size": size, "end": base_addr + size,
            "name": os.path.basename(name), "path": name,
        })
        off += 108
    modules.sort(key=lambda m: m["base"])
    return modules

def parse_thread_list(data, stream):
    off = stream["rva"]
    (num_threads,), off = read_struct(data, off, "<I")
    threads = []
    for _ in range(num_threads):
        (tid, suspend, pclass, priority), off = read_struct(data, off, "<IIII")
        off += 8  # teb
        stack_start = struct.unpack_from("<Q", data, off)[0]; off += 8
        (stack_size, stack_rva), off = read_struct(data, off, "<II")
        (ctx_size, ctx_rva), off = read_struct(data, off, "<II")
        threads.append({
            "tid": tid, "stack_start": stack_start,
            "stack_data_size": stack_size, "stack_rva": stack_rva,
            "ctx_size": ctx_size, "ctx_rva": ctx_rva,
        })
    return threads

# ── Address / module helpers ──────────────────────────────────────────────────

def addr_to_module(addr, modules):
    for m in modules:
        if m["base"] <= addr < m["end"]:
            return m["name"], addr - m["base"]
    return None, None

def scan_stack(data, thread, modules):
    rva, size = thread["stack_rva"], thread["stack_data_size"]
    if not rva or not size:
        return []
    hits = []
    stack_bytes = data[rva: rva + size]
    for i in range(0, len(stack_bytes) - 8, 8):
        val = struct.unpack_from("<Q", stack_bytes, i)[0]
        mn, mo = addr_to_module(val, modules)
        if mn:
            hits.append((val, mn, mo))
    return hits

# ── PDB symbolization ─────────────────────────────────────────────────────────

def _find_tool(name):
    homebrew = f"/opt/homebrew/opt/llvm/bin/{name}"
    return homebrew if os.path.exists(homebrew) else name

def build_symbol_map(pdb_path):
    """
    Dump public symbols from PDB via llvm-pdbutil and return a sorted list of
    (rva, name) pairs. Section 1 (.text) offsets are used as RVA approximations.
    """
    tool = _find_tool("llvm-pdbutil")
    try:
        result = subprocess.run(
            [tool, "dump", "--publics", pdb_path],
            capture_output=True, text=True, timeout=60
        )
    except (FileNotFoundError, subprocess.TimeoutExpired) as e:
        print(f"  [warn] {tool}: {e}", file=sys.stderr)
        return []

    symbols = []
    current_name = None
    for line in result.stdout.splitlines():
        line = line.strip()
        if line.startswith("demangled ="):
            current_name = line.split("=", 1)[1].strip()
        elif line.startswith("mangled =") and not current_name:
            current_name = line.split("=", 1)[1].strip()
        elif line.startswith("addr =") and current_name:
            addr_str = line.split("=", 1)[1].strip()
            try:
                section_str, offset_str = addr_str.split(":")
                section = int(section_str, 16)
                offset  = int(offset_str, 16)
                if section == 1:          # .text section → offset ≈ RVA
                    symbols.append((offset, current_name))
            except ValueError:
                pass  # ignore malformed addr lines in llvm-pdbutil output
            current_name = None

    symbols.sort(key=lambda x: x[0])
    return symbols

def lookup_symbol(rva, symbol_map):
    """Binary search: nearest symbol at or before rva."""
    if not symbol_map:
        return None, None
    lo, hi = 0, len(symbol_map) - 1
    idx = -1
    while lo <= hi:
        mid = (lo + hi) // 2
        if symbol_map[mid][0] <= rva:
            idx = mid
            lo = mid + 1
        else:
            hi = mid - 1
    if idx < 0:
        return None, None
    sym_rva, sym_name = symbol_map[idx]
    return sym_name, rva - sym_rva

# ── Formatted address label ───────────────────────────────────────────────────

def fmt_addr(addr, modules, symbol_map):
    mn, mo = addr_to_module(addr, modules)
    if not mn:
        return f"0x{addr:016X}"
    if mn.lower() == "magda.exe" and symbol_map:
        sym, fn_off = lookup_symbol(mo, symbol_map)
        if sym:
            return f"0x{addr:016X}  {sym}+0x{fn_off:X}"
    return f"0x{addr:016X}  {mn}+0x{mo:X}"

# ── Top-level parse ───────────────────────────────────────────────────────────

def parse(path, pdb_path=None):
    with open(path, "rb") as f:
        data = f.read()

    print(f"Minidump: {path}  ({len(data):,} bytes)")
    if pdb_path:
        print(f"PDB:      {pdb_path}")

    header  = parse_header(data)
    streams = parse_stream_directory(data, header)

    if SYSTEM_INFO_STREAM in streams:
        si = parse_system_info(data, streams[SYSTEM_INFO_STREAM])
        print(f"Arch: {si['arch']}  CPUs: {si['num_cpus']}")

    modules = parse_module_list(data, streams[MODULE_LIST_STREAM]) if MODULE_LIST_STREAM in streams else []

    symbol_map = []
    if pdb_path:
        symbol_map = build_symbol_map(pdb_path)
        if symbol_map:
            print(f"Symbols:  {len(symbol_map):,} public symbols loaded from PDB")
        else:
            print("Symbols:  [failed — check llvm-pdbutil is installed]")

    # ── Exception ────────────────────────────────────────────────────────────
    print("\n" + "="*70)
    print("EXCEPTION")
    print("="*70)
    exc = None
    if EXCEPTION_STREAM in streams:
        exc = parse_exception_stream(data, streams[EXCEPTION_STREAM])
        code_desc = EXCEPTION_CODES.get(exc["exc_code"], f"0x{exc['exc_code']:08X}")
        print(f"  Thread:  0x{exc['thread_id']:X}")
        print(f"  Code:    0x{exc['exc_code']:08X}  {code_desc}")
        print(f"  Address: {fmt_addr(exc['exc_address'], modules, symbol_map)}")

        if exc["exc_code"] == 0xC0000005 and exc["num_params"] >= 2:
            av_type = ACCESS_VIOLATION_TYPE.get(exc["exc_info"][0], f"type={exc['exc_info'][0]}")
            av_addr = exc["exc_info"][1]
            note = ""
            if av_addr == 0:                    note = "  ← NULL dereference"
            elif av_addr < 0x10000:             note = f"  ← small offset from NULL ({av_addr})"
            elif av_addr == 0xFFFFFFFFFFFFFFFF: note = "  ← sentinel/corruption (-1)"
            print(f"  AV:      {av_type} at 0x{av_addr:016X}{note}")

        if exc["ctx_rva"] and exc["ctx_size"]:
            regs = parse_x64_context(data, exc["ctx_rva"], exc["ctx_size"])
            if regs:
                print("\n  Registers:")
                for name in ["Rip","Rsp","Rbp","Rax","Rcx","Rdx","Rbx","Rsi","Rdi",
                             "R8","R9","R10","R11","R12","R13","R14","R15"]:
                    if name in regs:
                        print(f"    {name:>4} = {fmt_addr(regs[name], modules, symbol_map)}")
    else:
        print("  No exception stream found.")

    # ── Threads (summary) ────────────────────────────────────────────────────
    threads = parse_thread_list(data, streams[THREAD_LIST_STREAM]) if THREAD_LIST_STREAM in streams else []
    print("\n" + "="*70)
    print("THREADS")
    print("="*70)
    for t in threads:
        marker = " *** CRASHING ***" if exc and t["tid"] == exc["thread_id"] else ""
        regs = parse_x64_context(data, t["ctx_rva"], t["ctx_size"]) if t["ctx_rva"] else {}
        rip  = regs.get("Rip", 0)
        print(f"  TID=0x{t['tid']:X}{marker}  RIP={fmt_addr(rip, modules, symbol_map)}")

    # ── Stack scan (crashing thread) ─────────────────────────────────────────
    print("\n" + "="*70)
    print("STACK SCAN (crashing thread)")
    print("="*70)
    if exc and threads:
        ct = next((t for t in threads if t["tid"] == exc["thread_id"]), None)
        if ct:
            hits = scan_stack(data, ct, modules)
            seen = set()
            count = 0
            for addr, mn, mo in hits:
                key = (mn, mo)
                if key in seen:
                    continue
                seen.add(key)
                if mn.lower() == "magda.exe" and symbol_map:
                    sym, fn_off = lookup_symbol(mo, symbol_map)
                    label = f"{sym}+0x{fn_off:X}" if sym else f"{mn}+0x{mo:X}"
                else:
                    label = f"{mn}+0x{mo:X}"
                print(f"  0x{addr:016X}  {label}")
                count += 1
                if count >= 60:
                    print(f"  ... ({len(hits) - count} more)")
                    break

    # ── Module list ──────────────────────────────────────────────────────────
    print("\n" + "="*70)
    print("MODULES")
    print("="*70)
    for m in modules:
        print(f"  0x{m['base']:016X}  {m['name']}")

    print()

    # Return structured data for programmatic use (analyze-crash.py)
    return {
        "exc": exc,
        "modules": modules,
        "symbol_map": symbol_map,
    }


if __name__ == "__main__":
    import argparse
    ap = argparse.ArgumentParser(description="Parse a Windows minidump file")
    ap.add_argument("dump", help="Path to .dmp file")
    ap.add_argument("--pdb", help="Path to MAGDA.pdb for symbol resolution")
    args = ap.parse_args()
    parse(args.dump, pdb_path=args.pdb)
