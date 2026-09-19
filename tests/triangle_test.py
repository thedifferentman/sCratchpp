"""Compile the independent triangle library and compare it with the supplied kernel."""
import argparse,json,pathlib,subprocess,sys
ROOT=pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from build_stdlib import compiler_flags
p=argparse.ArgumentParser();p.add_argument('--compiler',required=True);p.add_argument('--clang',required=True);p.add_argument('--sdk',required=True);p.add_argument('--node',default='node');p.add_argument('--output-dir',default=str(ROOT/'build/validation/triangle'))
a=p.parse_args();out=pathlib.Path(a.output_dir).resolve();out.mkdir(parents=True,exist_ok=True)
cases=[[-120,-90,0,110,120,-90,0x4c97ff],[120,-90,0,110,-120,-90,0x4c97ff],[-20,-30,100,-30,-20,100,0x20a060],[-80,0,80,1,30,3,0x9933cc],[0,0,1,0,0,1,0xff8800],[0,0,40,0,80,0,0],[5,5,5,5,5,5,0]]
source=out/'main.cpp';source.write_text('#include <triangle/triangle.hpp>\nint main(){\n'+''.join('scratch::triangle::draw('+','.join(map(str,c))+');\n' for c in cases[:5])+'scratch::triangle::draw_fixed(-241,-181,1,221,241,-181,0x123456,2);\n'+''.join('scratch::triangle::draw('+','.join(map(str,c))+');\n' for c in cases[5:])+'return 0;}\n',encoding='utf-8')
def run(args):
 r=subprocess.run(list(map(str,args)),cwd=ROOT,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=120)
 if r.returncode:raise RuntimeError(r.stdout[-3000:]+'\n'+r.stderr[-3000:])
 return r.stdout
try:
 clang=str(pathlib.Path(a.clang).resolve());objects=[]
 for src in [source,ROOT/'include/triangle/triangle.cpp']:
  bc=out/(src.stem+'.bc');run([clang,*compiler_flags(clang,pathlib.Path(a.sdk).resolve()),'-std=c++17','-O1','-I',ROOT/'include','-emit-llvm','-c',src,'-o',bc]);objects.append(bc)
 target=out/'triangle.sb3';run([pathlib.Path(a.compiler).resolve(),*objects,'--whole-program','-o',target]);print(run([a.node,ROOT/'tests/triangle_vm.cjs',target,out/'report.json']))
except Exception as e:print(e,file=sys.stderr);sys.exit(1)
