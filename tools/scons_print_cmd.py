#!/usr/bin/env python3
"""Normalize SCons command printing for PlatformIO / SCons 4.11.1.

SCons 4.11.1 Action.__call__ does:

    cmd = self.strfunction(...)
    if cmd:
        print_func(cmd, target, source, env)

and the default print_cmd_line assumes cmd is a str:

    sys.stdout.write(s + "\\n")

CommandAction.cmdstr defaults to the SCons.Util.sctypes._null singleton.
deepcopy() (and pickle) of a CommandAction construct a *new* _Null instance
instead of preserving that singleton. Then:

    if self.cmdstr is not _null:   # True for the clone
        c = env.subst(self.cmdstr, ...)
        if c:                      # _Null is truthy (no __bool__)
            return c               # returns the sentinel

print_cmd_line then crashes with:
    TypeError: unsupported operand type(s) for +: '_Null' and 'str'
*before* execute() runs, so firmware.bin / similar actions never run.

This extra_script installs PRINT_CMD_LINE_FUNC. That hook only prints; it
does not decide whether the action executes. When the printed value is the
_Null sentinel (or any non-string), fall back to the Action string method
(str(action) / Action.__str__), which is the unsubstituted command template.
Do *not* use str(_Null) ("<SCons.Util.sctypes._Null object at 0x...>").
Do *not* run env.subst() here: that can hide a missing $VAR by expanding
it to empty, and a subst error in the printer would abort before execute().
Ordinary string cmd lines are printed unchanged.
"""

from __future__ import annotations

import sys


def is_scons_null(value) -> bool:
    """True for the SCons _Null sentinel or a deepcopy/pickle clone of it."""
    if value is None:
        return False
    cls = type(value)
    if cls.__name__ != "_Null":
        return False
    module = getattr(cls, "__module__", "") or ""
    return module.endswith("sctypes") or module.endswith("SCons.Util.sctypes")


def _iter_nodes(nodes):
    if nodes is None:
        return []
    if isinstance(nodes, (list, tuple)):
        return list(nodes)
    return [nodes]


def action_string(act):
    """Return Action.__str__(), never str(_Null)."""
    if act is None or is_scons_null(act):
        return None
    try:
        text = act.__str__()
    except Exception:
        return None
    if isinstance(text, str) and text:
        return text
    return None


def fallback_from_targets(target, source, env):
    texts = []
    seen = set()
    for node in _iter_nodes(target):
        get_ex = getattr(node, "get_executor", None)
        if not callable(get_ex):
            continue
        try:
            executor = get_ex()
        except Exception:
            continue
        if executor is None:
            continue
        get_list = getattr(executor, "get_action_list", None)
        actions = []
        if callable(get_list):
            try:
                actions = get_list() or []
            except Exception:
                actions = []
        for act in actions:
            key = id(act)
            if key in seen:
                continue
            seen.add(key)
            text = action_string(act)
            if text:
                texts.append(text)
    if texts:
        return "\n".join(texts)
    names = []
    for node in _iter_nodes(target):
        try:
            names.append(str(node))
        except Exception:
            continue
    if names:
        return "Building %s" % " and ".join(names)
    return None


def coerce_cmd_line(s, target, source, env):
    """Return a printable command string, or None to skip printing."""
    if isinstance(s, str):
        return s
    if isinstance(s, bytes):
        return s.decode("utf-8", "replace")
    # Sentinel / unexpected type: never str(s) when s is _Null.
    return fallback_from_targets(target, source, env)


def make_print_cmd_line(previous=None):
    def print_cmd_line(s, target, source, env):
        text = coerce_cmd_line(s, target, source, env)
        if not isinstance(text, str):
            return
        if previous is not None and getattr(previous, "_inkspeed_cmd_print", False):
            return previous(text, target, source, env)
        if previous is not None and previous is not print_cmd_line:
            return previous(text, target, source, env)
        sys.stdout.write(text + "\n")

    print_cmd_line._inkspeed_cmd_print = True
    return print_cmd_line


def install_print_normalizer(e):
    previous = e.get("PRINT_CMD_LINE_FUNC")
    if getattr(previous, "_inkspeed_cmd_print", False):
        return previous
    printer = make_print_cmd_line(previous)
    e.Replace(PRINT_CMD_LINE_FUNC=printer)
    return printer


def _scons_import():
    return globals().get("Import")


_import = _scons_import()
if callable(_import):
    _import("env")
    try:
        _import("projenv")
    except Exception:
        projenv = None  # noqa: F841  (SCons injects it on success)
    install_print_normalizer(env)
    if "projenv" in dir() and projenv is not None:
        install_print_normalizer(projenv)


def _self_test() -> None:
    class _Null:
        pass

    class FakeAction:
        def __init__(self, template):
            self.cmd_list = template

        def __str__(self):
            return self.cmd_list

    class FakeNode:
        def __init__(self, act, name="firmware.bin"):
            self._act = act
            self.name = name

        def get_executor(self):
            return self

        def get_action_list(self):
            return [self._act]

        def __str__(self):
            return self.name

    # Ordinary strings pass through unchanged (including $VARS).
    raw = 'esptool.py --chip esp32s3 elf2image -o "$TARGET" "$SOURCES"'
    assert coerce_cmd_line(raw, [], [], None) == raw
    missing = "$DOES_NOT_EXIST $TARGET"
    assert coerce_cmd_line(missing, [], [], None) == missing

    # Bytes are decoded, not rejected.
    assert coerce_cmd_line(b"echo ok", [], [], None) == "echo ok"

    # A _Null-like sentinel must not become str(_Null).
    sentinel = _Null()
    ugly = str(sentinel)
    assert "_Null" in type(sentinel).__name__
    node = FakeNode(FakeAction("echo hello $TARGET"))
    got = coerce_cmd_line(sentinel, [node], [], None)
    assert isinstance(got, str), got
    assert got == "echo hello $TARGET"
    assert ugly not in got
    assert "_Null object" not in got

    # Missing construction vars stay visible in the Action string fallback.
    node2 = FakeNode(FakeAction("$ELF2BINCMD"))
    got2 = coerce_cmd_line(sentinel, [node2], [], None)
    assert got2 == "$ELF2BINCMD"

    # No action / no target: skip print rather than crash.
    assert coerce_cmd_line(sentinel, [], [], None) is None

    # Printer must not raise on a sentinel, and must not call through
    # an existing hook with a non-str (that hook may do s+"\\n").
    seen = []

    def previous_printer(s, target, source, env):
        seen.append(s)
        # Deliberately the fragile SCons default: would TypeError on _Null.
        _ = s + "\n"

    printer = make_print_cmd_line(previous_printer)
    printer(sentinel, [node], [], None)
    assert seen == ["echo hello $TARGET"]

    # Real SCons deepcopy reproduction, if tool-scons is present.
    import os
    from pathlib import Path

    pio_roots = [Path.home() / ".platformio"]
    core = os.environ.get("PLATFORMIO_CORE_DIR")
    if core:
        pio_roots.append(Path(core))
    scons_roots = []
    for pio_root in pio_roots:
        scons_roots.extend(pio_root.glob("packages/tool-scons/scons-local-*"))
    if scons_roots:
        import copy

        sys.path.insert(0, str(sorted(scons_roots)[-1]))
        from SCons.Action import Action
        from SCons.Util.sctypes import _null, _Null as SConsNull

        act = Action("echo hello $TARGET")
        assert act.cmdstr is _null
        cloned = copy.deepcopy(act)
        assert cloned.cmdstr is not _null
        assert isinstance(cloned.cmdstr, SConsNull)
        assert is_scons_null(cloned.cmdstr)

        class DummyEnv(dict):
            def subst(self, s, *a, **k):
                return s

            def subst_list(self, s, *a, **k):
                return [[s]]

        cmd = cloned.strfunction(["firmware.bin"], ["app.elf"], DummyEnv())
        assert is_scons_null(cmd), type(cmd)
        # Default SCons printer would raise here:
        try:
            _ = cmd + "\n"
            raise AssertionError("expected TypeError from _Null + str")
        except TypeError:
            pass
        # Our fallback uses Action.__str__, which keeps $TARGET.
        node3 = FakeNode(cloned)
        got3 = coerce_cmd_line(cmd, [node3], [], None)
        assert got3 == "echo hello $TARGET", got3

        # End-to-end: default print_cmd_line aborts BEFORE execute();
        # PRINT_CMD_LINE_FUNC lets the real action run.
        class CallEnv(dict):
            def subst(self, s, *a, **k):
                return s

            def subst_list(self, s, *a, **k):
                return [[s]]

            def Replace(self, **kw):
                self.update(kw)

        ran = []

        def fake_execute(target, source, env, executor=None):
            ran.append("executed")
            return 0

        raw_env = CallEnv()
        broken = copy.deepcopy(Action("echo hello $TARGET"))
        broken.execute = fake_execute
        try:
            broken(["firmware.bin"], ["app.elf"], raw_env, show=1, execute=1, presub=0)
            raise AssertionError("expected TypeError without PRINT_CMD_LINE_FUNC")
        except TypeError as exc:
            assert "_Null" in type(exc).__name__ or "Null" in str(exc) or "+" in str(exc)
        assert ran == []

        fixed_env = CallEnv()
        install_print_normalizer(fixed_env)
        broken2 = copy.deepcopy(Action("echo hello $TARGET"))
        broken2.execute = fake_execute
        import io
        from contextlib import redirect_stdout
        with redirect_stdout(io.StringIO()) as buf:
            rc = broken2(["firmware.bin"], ["app.elf"], fixed_env, show=1, execute=1, presub=0)
        assert rc == 0, rc
        assert ran == ["executed"]
        printed = buf.getvalue()
        assert isinstance(printed, str)
        assert "_Null object" not in printed

    print("scons_print_cmd self-test: ok")


if __name__ == "__main__":
    _self_test()
