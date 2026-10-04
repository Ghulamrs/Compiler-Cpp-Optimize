"""scen.py <windows|linux> <stamp> - package and launch the two scenarios on one box under <stamp>:
native.sh (scenario 2: cpp11 -O2 native against cl /O2 or g++ -O2) and then rep.sh (scenario 1: cpp11 -O2
images on the CCS 5.5 simulator, a test lane of 10 passes beside a reference lane of 2). rounds.sh starts
rounds; scen_collect.sh and scen_report.py gather and average them."""
import sys, os, shutil, tarfile, tempfile, time
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, os.path.dirname(HERE)); import o2run
REPO = os.path.normpath(os.path.join(HERE, '..', '..', '..'))
box, stamp = sys.argv[1], sys.argv[2]
work = tempfile.mkdtemp(); kit = os.path.join(work, stamp); os.makedirs(kit)
names = o2run.stage_programs(os.path.join(kit, 'progs'))
for f in ('o2run.sh','rep.sh','native.sh','collect.sh','runbatch.js','C6747.cmd','C6747-ddr.cmd','c6747ca-windows.ccxml','c6747ca-linux.ccxml','start-detached.ps1'):
    shutil.copy(os.path.join(o2run.KIT, f), kit)
shutil.copytree(os.path.join(o2run.KIT, 'expected-ex'), os.path.join(kit, 'expected-ex'))
shutil.copy(os.path.join(REPO, 'tools', 'windows', 'bench-kernels.cpp'), kit)
shutil.copytree(os.path.join(REPO, 'tools', 'c6747', 'compilerpp', 'workload'), os.path.join(kit, 'workload'))
sel = [n for n in names if not n.startswith('case-') and n not in ('compilerpp-harness','ex-compilerpp')]
tags = ['cpp11-O2','744','822'] if box == 'windows' else ['cpp11-O2','744']
open(os.path.join(kit,'plan-build.txt'),'w',newline='\n').write(''.join(f'{n} {t}\n' for n in sel for t in tags))
open(os.path.join(kit,'plan-sim.txt'),'w',newline='\n').write('')
tgz = os.path.join(work, 'k.tgz')
with tarfile.open(tgz,'w:gz') as t: t.add(kit, arcname=stamp)
B = o2run.BOXES[box]
if box == 'windows':
    o2run.run(B['scp'] + [tgz, f'windows:C:/cxx1/o2run/{stamp}.tgz'])
    o2run.run(B['ssh'] + [f'cd /d C:\\cxx1\\o2run && tar -xzf {stamp}.tgz && del {stamp}.tgz'])
    cmd = os.path.join(work,'run.cmd')
    open(cmd,'w',newline='\r\n').write(f'@echo off\ncall "C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\VC\\Auxiliary\\Build\\vcvars64.bat" >nul\n'
        f'"C:\\Program Files\\Git\\bin\\bash.exe" C:/cxx1/o2run/{stamp}/native.sh windows > C:\\cxx1\\o2run\\{stamp}\\native.log 2>&1\n'
        f'"C:\\Program Files\\Git\\bin\\bash.exe" C:/cxx1/o2run/{stamp}/rep.sh windows > C:\\cxx1\\o2run\\{stamp}\\rep.log 2>&1\n')
    o2run.run(B['scp'] + [cmd, f'windows:C:/cxx1/o2run/{stamp}/run.cmd'])
    r = o2run.run(B['ssh'] + [f'powershell -NoProfile -ExecutionPolicy Bypass -File C:\\cxx1\\o2run\\{stamp}\\start-detached.ps1 C:\\cxx1\\o2run\\{stamp}\\run.cmd'])
else:
    o2run.run(B['scp'] + [tgz, f'{B["host"]}:o2run/{stamp}.tgz'])
    r = o2run.run(B['ssh'] + [f'cd o2run && tar xzf {stamp}.tgz && rm {stamp}.tgz && cd {stamp} && chmod +x *.sh && (nohup setsid bash -c "./native.sh linux > native.log 2>&1; ./rep.sh linux > rep.log 2>&1" > /dev/null 2>&1 < /dev/null &)'])
print(box, stamp, 'launched', r.returncode, time.strftime('%H:%M:%S'))
