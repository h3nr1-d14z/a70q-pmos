import re
p = "/work/kernel/samsung/sm6150/scripts/gcc-wrapper.py"
s = open(p).read()
# python2 print >> -> python3 print()
s = s.replace("            print >> sys.stderr, line,\n",
              "            print(line, file=sys.stderr, end=' ')\n")
s = s.replace("            print >> sys.stderr, args[0] + ':',e.strerror\n",
              "            print(args[0] + ':', e.strerror, file=sys.stderr)\n")
s = s.replace("            print >> sys.stderr, 'Is your PATH set correctly?'\n",
              "            print('Is your PATH set correctly?', file=sys.stderr)\n")
s = s.replace("            print >> sys.stderr, ' '.join(args), str(e)\n",
              "            print(' '.join(args), str(e), file=sys.stderr)\n")
assert "print >>" not in s, "print >> still present"
open(p, "w").write(s)
print("wrapper converted to python3")
# sanity compile check
r = subprocess  # noqa
import subprocess as sp
r2 = sp.run("python3 -m py_compile /work/kernel/samsung/sm6150/scripts/gcc-wrapper.py",
            shell=True, capture_output=True)
assert r2.returncode == 0, "wrapper still has syntax errors: " + r2.stderr.decode()
print("py_compile OK")
