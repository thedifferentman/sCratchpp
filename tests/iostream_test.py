"""Real narrow libc++ streams and numeric conversion regression in both VMs."""
import argparse,json,pathlib,subprocess,sys,time
ROOT=pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from build_stdlib import compiler_flags
p=argparse.ArgumentParser()
p.add_argument('--compiler',required=True);p.add_argument('--clang',required=True);p.add_argument('--sdk',required=True)
p.add_argument('--node',default='node');p.add_argument('--vm',choices=['both','scratch','turbowarp'],default='both')
p.add_argument('--timeout',type=int,default=600000)
p.add_argument('--check-missing-only', action='store_true')
p.add_argument('--output-dir',default=str(ROOT/'build/validation/iostream'))
a=p.parse_args();out=pathlib.Path(a.output_dir).resolve();out.mkdir(parents=True,exist_ok=True)
report={'passed':False,'runs':[]}
def run(args,timeout=180):
 r=subprocess.run([str(x) for x in args],cwd=ROOT,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=timeout)
 if r.returncode:raise RuntimeError(r.stderr[-4000:] or r.stdout[-4000:])
 return r.stdout
try:
 sdk=pathlib.Path(a.sdk).resolve();clang=str(pathlib.Path(a.clang).resolve())
 missing=out/'missing-adapter.cpp'
 missing.write_text('#include <iostream>\nint main(){std::cout << "hello";}\n',encoding='utf-8')
 run([clang,*compiler_flags(clang,sdk),'-std=c++17','-O1','-emit-llvm','-c',missing,'-o',out/'missing-adapter.bc'])
 absent=subprocess.run([str(pathlib.Path(a.compiler).resolve()),str(out/'missing-adapter.bc'),str(sdk/'lib/scratch-stdlib.bc'),
     '--whole-program','--memory','200000','-o',str(out/'missing-adapter.sb3')],capture_output=True,text=True,encoding='utf-8',timeout=120)
 if absent.returncode==0 or 'terminal adapter is missing' not in absent.stderr:
  raise RuntimeError('Missing terminal must fail clearly: '+absent.stderr[-2000:])
 report['missingAdapterRejected']=True
 if a.check_missing_only:
  report['passed']=True
  (out/'report.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
  print('Missing stdio adapter correctly rejected')
  sys.exit(0)
 print('Compiling iostream fixture',flush=True)
 run([clang,*compiler_flags(clang,sdk),'-std=c++17','-O2','-emit-llvm','-c',ROOT/'tests/iostream.cpp','-o',out/'test.bc'])
 print('Lowering iostream fixture',flush=True)
 run([pathlib.Path(a.compiler).resolve(),out/'test.bc',sdk/'lib/scratch-stdlib.bc','--whole-program','--passes','default<O2>','--memory','200000','-o',out/'test.sb3'])
 for vm in (['turbowarp','scratch'] if a.vm=='both' else [a.vm]):
  print('Running '+vm,flush=True)
  data=json.loads(run([a.node,ROOT/'tests/vm_runner.cjs',out/'test.sb3','--vm',vm,'--timeout',a.timeout,'--list-limit','4'],a.timeout//1000+45))
  (out/(vm+'.json')).write_text(json.dumps(data,ensure_ascii=False),encoding='utf-8')
  program=next(t for t in data['targets'] if t['name']=='Program');v=program['variables']
  item={'vm':vm,'status':v.get('__scl_status'),'exit':v.get('exit_code'),'last':v.get('iostream_last'),'executionMs':data.get('executionMs')};report['runs'].append(item)
  if item['status']!='done' or item['exit']!=0 or item['last']!=ord('Z'):raise RuntimeError(str(item))
 report['passed']=True
except Exception as e:report['error']=str(e)
(out/'report.json').write_text(json.dumps(report,indent=2,ensure_ascii=False),encoding='utf-8')
print(json.dumps(report,ensure_ascii=False))
sys.exit(0 if report['passed'] else 1)
