"""Translate the supplied C1 kernel to the compiler's Scratch inline-assembly dialect.
The project is read as data, never executed. Demo hats/sprites and empty comment calls
are excluded; the two-argument motion helper is expanded into the eight-layer kernel.
"""
import argparse, hashlib, json, pathlib, zipfile

def extract(source, output):
    raw=source.read_bytes()
    with zipfile.ZipFile(source) as z: data=z.read('project.json')
    project=json.loads(data)
    matches=[]
    for target in project['targets']:
        blocks=target.get('blocks',{});procedures={}
        for b in blocks.values():
            if b['opcode']=='procedures_definition':
                proto=blocks[b['inputs']['custom_block'][1]]['mutation']
                procedures[proto['proccode']]=(proto,b.get('next'))
        for code,(proto,head) in procedures.items():
            if json.loads(proto['argumentnames'])==['Ax','Ay','Bx','By','Cx','Cy']:
                matches.append((target,procedures,code))
    if len(matches)!=1:raise ValueError('Expected one six-coordinate triangle kernel')
    target,procedures,entry=matches[0];blocks=target['blocks']
    suffixes={'内心x':'ix','内心y':'iy','边长a':'a','边长b':'b','边长c':'c','周长':'semiperimeter','内接圆半径':'diameter','面积':'area','倍数k':'scale'}
    variables={v[0]:'__scl_triangle_'+suffixes[v[0].removeprefix('<画三角形>')] for v in target['variables'].values()}
    if len(variables)!=9:raise ValueError('Unexpected triangle workspace')
    def literal(x):return json.dumps(x,ensure_ascii=False)
    def reporter(value,args):
        if isinstance(value,list):
            if value[0]==12:return '(data_variable VARIABLE='+literal(variables[value[1]])+')'
            if value[0]==13:raise ValueError('Unexpected list reference')
            return literal(value[1])
        b=blocks[value]
        if b['opcode'].startswith('argument_reporter'):return args[b['fields']['VALUE'][0]]
        if not b['opcode'].startswith(('operator_','data_variable')):raise ValueError('Unexpected reporter '+b['opcode'])
        fields=[]
        for name,v in b['fields'].items():fields.append(name+'='+literal(variables[v[0]] if name=='VARIABLE' else v[0]))
        return '('+b['opcode']+' '+ ' '.join([k+'='+reporter(v[1],args) for k,v in b['inputs'].items()]+fields)+')'
    def stack(head,args,depth=0):
        if depth>8:raise ValueError('Unexpected recursive procedure')
        result=[];seen=set()
        while head:
            if head in seen:raise ValueError('Cyclic block chain')
            seen.add(head);b=blocks[head];op=b['opcode']
            if op=='procedures_call':
                proto,body=procedures[b['mutation']['proccode']]
                binding={name:reporter(b['inputs'][key][1],args) for key,name in zip(json.loads(proto['argumentids']),json.loads(proto['argumentnames']))}
                result.extend(stack(body,binding,depth+1))
            else:
                if op not in ['data_setvariableto','motion_gotoxy','pen_penUp','pen_penDown','pen_setPenSizeTo','control_repeat']:raise ValueError('Unexpected command '+op)
                parts=[op]
                for k,v in b['inputs'].items():
                    parts.append(k+'=%{ '+ ' '.join(stack(v[1],args,depth+1))+' %}' if k.startswith('SUBSTACK') else k+'='+reporter(v[1],args))
                for k,v in b['fields'].items():parts.append(k+'='+literal(variables[v[0]] if k=='VARIABLE' else v[0]))
                result.append(' '.join(parts)+';')
            head=b.get('next')
        return result
    args={n:'(operator_divide NUM1=%'+str(i)+' NUM2=%7)' for i,n in enumerate(['Ax','Ay','Bx','By','Cx','Cy'])}
    commands=stack(procedures[entry][1],args)
    repeat=next(i for i,s in enumerate(commands) if s.startswith('control_repeat '))
    guard='control_if CONDITION=(operator_and OPERAND1=(operator_gt OPERAND1=(data_variable VARIABLE="__scl_triangle_area") OPERAND2=0) OPERAND2=(operator_gt OPERAND1=(data_variable VARIABLE="__scl_triangle_diameter") OPERAND2=0)) SUBSTACK %{ '
    commands=['pen_penUp;','pen_setPenColorToColor COLOR=%6;']+commands[:repeat]+[guard+commands[repeat]+' %};','pen_penUp;']
    output.mkdir(parents=True,exist_ok=True)
    code='#include "triangle.hpp"\n\n// Generated from the supplied C1 Scratch kernel by tools/extract_triangle.py.\nnamespace scratch::triangle {\nvoid draw_fixed(int ax,int ay,int bx,int by,int cx,int cy,unsigned rgb,int units_per_pixel) {\n    if(units_per_pixel<=0)return;\n    asm volatile(\n'
    code+=''.join('        '+json.dumps(c,ensure_ascii=False)+'\n' for c in commands)
    code+='        : : "r"(ax), "r"(ay), "r"(bx), "r"(by), "r"(cx), "r"(cy), "r"(rgb), "r"(units_per_pixel) : "memory");\n}\nvoid draw(int ax,int ay,int bx,int by,int cx,int cy,unsigned rgb) { draw_fixed(ax,ay,bx,by,cx,cy,rgb,1); }\n}\n'
    (output/'triangle.cpp').write_text(code,encoding='utf-8')
    (output/'SOURCE.json').write_text(json.dumps({'source_filename':source.name,'source_sha256':hashlib.sha256(raw).hexdigest(),'project_json_sha256':hashlib.sha256(data).hexdigest(),'procedure':entry,'layers':8,'changes':['Namespaced temporary variables','Inlined motion helper and removed empty comment calls','Added explicit pen-up/color entry and pen-up exit','Nonpositive area/diameter skips drawing','Added fixed-point input adapter; native coordinate division precedes the original kernel'],'license':'Unspecified in supplied project; no ownership or redistribution license is asserted.'},ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    # Minimal readable recipe for regression, without demo sprites or embedded assets.
    needed=set()
    def visit(k):
        if not k or k in needed:return
        needed.add(k);b=blocks[k];visit(b.get('next'))
        for value in b['inputs'].values():
            if isinstance(value[1],str):visit(value[1])
    for k,b in blocks.items():
        if b['opcode']=='procedures_definition':visit(k)
    target={**target,'blocks':{k:blocks[k] for k in needed},'comments':{}}
    (output/'kernel.json').write_text(json.dumps({'entry':entry,'target':target},ensure_ascii=False,indent=2)+'\n',encoding='utf-8')

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('source',type=pathlib.Path);p.add_argument('--output-dir',required=True,type=pathlib.Path);a=p.parse_args();extract(a.source,a.output_dir)
