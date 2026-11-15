"""Which decomp source the port builds from.

tools/gametrial/decomp_ref.txt names a commit of the matching repo (excite-truck): its src/, include/ and config/
are exported read-only with `git archive` into build/decomp/<commit>/ and every port tool reads from there, so the
port does not pick up other sessions' work in progress (the working tree changes under us all day). `live` (or no
file) reads the working tree directly. Update the pin on purpose: tools/gametrial/pin.sh [commit|HEAD].
"""
import io, os, subprocess, tarfile

HERE = os.path.dirname(os.path.abspath(__file__))
PORT = os.path.normpath(os.path.join(HERE, '..', '..'))
LIVE = os.path.normpath(os.path.join(PORT, '..', 'excite-truck'))
REF_FILE = os.path.join(HERE, 'decomp_ref.txt')


def ref():
    if not os.path.exists(REF_FILE):
        return 'live'
    return open(REF_FILE).read().split()[0] if open(REF_FILE).read().split() else 'live'


def root():
    r = ref()
    if r == 'live':
        return LIVE
    out = os.path.join(PORT, 'build', 'decomp', r[:12])
    if not os.path.isdir(os.path.join(out, 'src')):
        tar = subprocess.run(['git', '-C', LIVE, 'archive', '--format=tar', r, 'src', 'include', 'config'],
                             capture_output=True, creationflags=0x08000000)
        if tar.returncode:
            raise SystemExit('decomp.py: git archive %s failed: %s' % (r, tar.stderr.decode(errors='replace')))
        os.makedirs(out, exist_ok=True)
        with tarfile.open(fileobj=io.BytesIO(tar.stdout)) as t:
            t.extractall(out, filter='data')
    return out
