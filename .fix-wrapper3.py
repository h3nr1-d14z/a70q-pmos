import subprocess, os, re
p = "/work/kernel/samsung/sm6150/scripts/gcc-wrapper.py"
s = open(p).read()
# python2 print >> -> python3 print(), bat ky indent nao (space/tab)
s = re.sub(r"^([ \t]+)print >> sys\.stderr, line,\n",
           r"\1print(line, file=sys.stderr, end=' ')\n", s, flags=re.M)
s = re.sub(r"^([ \t]+)print >> sys\.stderr, args\[0\] \+ ':',e\.strerror\n",
           r"\1print(args[0] + ':', e.strerror, file=sys.stderr)\n", s, flags=re.M)
s = re.sub(r"^([ \t]+)print >> sys\.stderr, 'Is your PATH set correctly\?'\n",
           r"\1print('Is your PATH set correctly?', file=sys.stderr)\n", s, flags=re.M)
s = re.sub(r"^([ \t]+)print >> sys\.stderr, ' '\.join\(args\), str\(e\)\n",
           r"\1print(' '.join(args), str(e), file=sys.stderr)\n", s, flags=re.M)
assert "print >>" not in s, "print >> still present"
open(p, "w").write(s)
r = subprocess.run("python3 -m py_compile " + p, shell=True, capture_output=True)
assert r.returncode == 0, "syntax errors remain: " + r.stderr.decode()
st = os.stat(p)
os.utime(p, None)
print("wrapper py3 OK | mtime:", time.ctime(st.st_mtime))
