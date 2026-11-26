#!/bin/bash
# tree-current.sh PLAN9 OBJTYPE: is the tree's APE archive of OBJTYPE built
# from its headers as they are?  A program compiled against the headers and
# linked against an archive of another state breaks without a word when the
# two differ in a structure (APE's struct stat, 2026-11-26: the native ld's
# stat wrote past its buffer).  Fails when a header of sys/include/ape or
# OBJTYPE/include/ape is newer than OBJTYPE/lib.gcc/ape/libap.a: build the
# tree (CC=gcc ARCH=OBJTYPE build.exe) first.  Used by sysroot.sh, build.sh
# native and toolchain/clang's sysroot and runtimes stages.
P=${1:?usage: tree-current.sh PLAN9 OBJTYPE} o=${2:?usage: tree-current.sh PLAN9 OBJTYPE}
A=$P/$o/lib.gcc/ape/libap.a
[ -f "$A" ] || { echo "tree-current: no $A (build the tree first)" >&2; exit 1; }
n=$(find "$P/sys/include/ape" "$P/$o/include/ape" -name '*.h' -newer "$A" 2>/dev/null | head -5)
[ -z "$n" ] || {
	echo "tree-current: headers newer than $A (build the tree: CC=gcc ARCH=$o build.exe):" >&2
	echo "$n" | sed 's/^/	/' >&2
	exit 1
}
