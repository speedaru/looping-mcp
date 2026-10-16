"""Show where two .loo files differ.

usage: python -I tools/loo_diff.py A.loo B.loo [--ctx N]

Uses difflib on the raw bytes so insertions (e.g. a new attribute record) are reported
as insertions, not as a cascade of shifted differences.
"""
import difflib
import sys


def show(a, b, ctx=8):
    sm = difflib.SequenceMatcher(None, a, b, autojunk=False)
    for tag, i1, i2, j1, j2 in sm.get_opcodes():
        if tag == "equal":
            continue
        print(f"{tag:8s} A[{i1:#06x}:{i2:#06x}] ({i2 - i1} B)  ->  B[{j1:#06x}:{j2:#06x}] ({j2 - j1} B)")
        if i2 > i1:
            print("   A:", a[max(i1 - ctx, 0):i1].hex(" "), "|", a[i1:i2][:96].hex(" "))
        if j2 > j1:
            print("   B:", b[max(j1 - ctx, 0):j1].hex(" "), "|", b[j1:j2][:96].hex(" "))


if __name__ == "__main__":
    args = [x for x in sys.argv[1:] if not x.startswith("--")]
    ctx = int(sys.argv[sys.argv.index("--ctx") + 1]) if "--ctx" in sys.argv else 8
    args = [x for x in args if not x.isdigit()]
    da, db_ = open(args[0], "rb").read(), open(args[1], "rb").read()
    print(f"A={args[0]} ({len(da)} B)  B={args[1]} ({len(db_)} B)")
    show(da, db_, ctx)
