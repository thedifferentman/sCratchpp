# Scratch 内联汇编

本模块将 LLVM inline asm 的模板转换为 Scratch 官方积木。opcode 使用 `operator_add`、`data_itemoflist`、`pen_penDown` 等官方内部名称，不带 `scratch:` 前缀。它没有执行 JavaScript、插件代码或原生机器指令的通道。

实现入口是 `emit_assembly`，返回待执行积木和结果字节。调用者按正常 SSA 结果机制保存这些字节。所有桥接字节采用后端值的低字节在前顺序，与对象实际内存端序分开处理。

前端可用无状态的 `validate_assembly_template(text)` 检查整个模块内的每一处模板，包括未使用函数中的汇编。它解析全部语句和嵌套子脚本，检查 opcode、绑定及结构，不会只检查第一个 opcode；输入索引、LLVM 类型、约束、副作用和输出确定赋值在 `emit_assembly` 阶段完成。

## 约束与数值接口

当前支持一个直接输出 `=r` 或 `=&r`、任意个 `r` 输入，以及末尾可选的 clobber。输出约束排在输入之前，clobber 排在所有操作数约束之后；clobber 不占操作数编号。操作数约束数量必须与 LLVM 实际参数和返回类型一致。

接受的 clobber 是 `~{memory}`、`~{dirflag}`、`~{fpsr}`、`~{flags}` 和 `~{cc}`。Clang 以 x86 为源目标生成 LLVM IR 时，即使 C 汇编模板只包含官方 Scratch opcode，也可能自动附加 `dirflag/fpsr/flags` 这三类状态 clobber。它们及条件码 `~{cc}` 在 Scratch 中没有对应 CPU 状态，因此不产生目标操作；`~{memory}` 仍在 LLVM 优化阶段保留内存屏障含义。通用寄存器 clobber，例如 `~{rax}`、`~{x0}`，以及其他未列出的 clobber 仍明确拒绝。相同 clobber 不能重复声明。

| LLVM 签名 | 模板中的编号 |
|---|---|
| 有返回值 | `$0` 为输出，`$1` 为第一个输入，依次增加 |
| `void` 返回 | `$0` 为第一个输入，依次增加；没有可赋值的输出 |

输入类型限 `i1` 至 `i32`；输出支持这些整数以及 `double`（IEEE binary64）。`i2` 至 `i32` 输入按有符号二进制补码解码；`i1` 输入保留数字 `0/1`，不解码成 `0/-1`。所有 LLVM 输入在模板开始前读取并保存，后面的列表或变量修改不会让输入重新求值。

整数输出先按 Scratch 普通数值转换，再向零截断，并对 `2^位宽` 取模，最后拆成字节。因此 `-2.5 → i32` 得到 `0xfffffffe`，`128 → i8` 得到 `0x80`。布尔值成为 `0/1`；无法转换的文本、NaN 和正负无穷转换为零。非整字节整数的最高字节只保留有效位。

`double` 输出将 Scratch 原生数值精确编码为 8 个 IEEE 字节，保留小数、次正规数、正负零与无穷；NaN 输出规范 quiet NaN（不保留 payload），无法转换的其他文本为零。例如 `asm volatile("sensing_dayssince2000" : "=r"(days));` 中 `days` 可声明为 `double`，不会丢失日期中的时间小数。此转换不依赖 SoftFloat；之后的 LLVM 浮点计算仍走正常的精确运行时。

支持布尔参数报告器 `argument_reporter_boolean VALUE="is turbowarp?"`，供 `scratch::is_turbowarp()` 使用。TW 对这个未绑定的特殊参数返回真（包括解释模式），原版返回零。输出仍是原版核心 opcode，不引入 `tw` 扩展依赖；`is_turbowarp` 是 C++ 接口名称，不是另造的积木 opcode。

这是 Scratch 原生 opcode 的数值接口。模板中的乘除等仍采用 Scratch 本身的数值语义；例如很大的整数乘法可能失去低位精度。需要完整 LLVM 整数语义的程序应使用普通 LLVM 运算，让字节后端处理。

需要 C++ 字符串输入时，SDK 的 `scratch::set_string(const std::string&)` 会将 UTF-8 转换到原生变量 `__scl_string`，然后在汇编中读取它。例如：

```cpp
scratch::set_string(name);
asm volatile("looks_switchcostumeto COSTUME=(data_variable VARIABLE=\"__scl_string\")");
```

这不改变 `r` 操作数的整数限制，也不将字符串作为可执行汇编解释。造型相关 opcode 包括 `looks_switchcostumeto COSTUME=...`、`looks_nextcostume`、`looks_costumenumbername NUMBER_NAME="number"`（或 `"name"`）。`__scl_unicode` 是字符串转换按需加入的只读常量表，不能通过汇编修改。

目前明确拒绝浮点输入、`float` 输出、指针、聚合值以及超过 32 位的整数桥接；这些诊断不会妨碍普通 LLVM IR 中由其他模块支持的相应类型。也拒绝多输出、读写/绑定输出、固定寄存器、内存操作数、立即数专属约束以及其他 clobber。

## 模板语法

```text
program   := statement (';' statement)* [';']
statement := ['$0' '='] opcode binding*
binding   := NAME '=' value
           | SUBSTACK ['='] '{' program '}'
           | SUBSTACK2 ['='] '{' program '}'
value     := '$' INTEGER
           | JSON_NUMBER
           | JSON_STRING
           | 'true' | 'false'
           | '(' opcode binding* ')'
```

花括号可以为空。语句以分号分隔；换行只是空白。字段、输入及子脚本的名称区分大小写。数字和字符串采用 JSON 字面量写法，文本字段必须用双引号。字符串中的分号、美元符号和花括号都是普通文本。

解析器拒绝未知 opcode、缺少或重复的输入、未知字段、多余 token、不平衡括号和超过 128 层的嵌套。它不会尝试猜测 x86、ARM 等机器汇编的意思。

若整个模板只有一个没有字段和子脚本的裸 opcode，按下表的标准输入顺序绑定所有 LLVM 参数。例如 `operator_add` 等价于 `operator_add NUM1=$1 NUM2=$2`（有返回值时）。参数数量必须正好匹配。

单独的 reporter 可以隐式写入输出：

```text
operator_add NUM1=$1 NUM2=$2
```

在多语句模板中，reporter 必须明确赋给 `$0`：

```text
$0 = operator_add NUM1=$1 NUM2=$2;
$0 = operator_multiply NUM1=$0 NUM2=2
```

`$0` 可以多次赋值，但只能在已经赋值的路径上读取。中间 `$0` 保存 reporter 的 Scratch 值；最终整数转换在整个模板结束后进行。编译器检查每个可能结束的路径都已定义输出：`if_else` 的两支都赋值可以通过；只在 `if` 或循环内部首次赋值会报错，因为该分支或循环可能不执行。`void` 模板不能使用输出赋值。

控制积木和嵌套 reporter 示例：

```text
data_setvariableto VARIABLE="sum" VALUE=0;
control_repeat TIMES=$1 SUBSTACK {
    data_changevariableby VARIABLE="sum" VALUE=2;
};
control_if_else CONDITION=(data_itemoflist LIST="flags" INDEX=1)
    SUBSTACK { $0 = data_variable VARIABLE="sum"; }
    SUBSTACK2 { $0 = operator_subtract NUM1=0 NUM2=(data_variable VARIABLE="sum"); }
```

列表 reporter 可以直接进入布尔输入。`VARIABLE="name"` 和 `LIST="name"` 是符号引用：复用同名声明；没有声明时分别创建初值为零的变量或空列表。同一个项目中的相同名称指向同一个符号。`__scl_asm` 前缀保留给桥接临时变量。文本常量和上述 Scratch 符号属于汇编接口，不是 LLVM 字符串指针转换。

## 副作用与屏障

修改变量、列表、位置、外观、画笔、计时器等有可观察副作用的 opcode，以及随机数操作，需要 LLVM `asm sideeffect`。缺少该标记会给出诊断。读取已有状态的 reporter 可以提供返回值，但如果要求每次都实际读取、不能由上游优化删除或合并，应使用 `sideeffect`。

读写 LLVM 虚拟内存对应的列表时，还应声明 `~{memory}`，供 LLVM 优化阶段理解内存影响。该标记不生成额外运行积木。空模板仅接受无结果、无输入，以及空约束或上述受支持的 clobber：

```llvm
call void asm sideeffect "", "~{memory}"()

%r = call i32 asm "operator_add", "=r,r,r"(i32 -7, i32 2)

call void asm sideeffect "pen_penDown; motion_gotoxy X=$0 Y=$1; pen_penUp",
    "r,r"(i32 -7, i32 4)
```

## 从 C 经 Clang 生成 IR

C 的 GNU 扩展汇编使用 `%0`、`%1` 引用操作数；Clang 生成 LLVM IR 后，它们才成为 `$0`、`$1`。不要把 LLVM 模板中的美元编号直接复制到 C 扩展汇编：Clang 可能把字面量美元转义为 `$$`，它不是本方言的操作数。

```c
int scratch_add(int left, int right) {
    int result;
    __asm__("operator_add NUM1=%1 NUM2=%2"
            : "=r"(result) : "r"(left), "r"(right));
    return result;
}

void scratch_line_to(int x, int y) {
    __asm__ volatile("pen_penDown; motion_gotoxy X=%0 Y=%1; pen_penUp"
                     : : "r"(x), "r"(y));
}

void scratch_memory_barrier(void) {
    __asm__ volatile("" : : : "memory");
}
```

例如 `clang -S -emit-llvm -O1 drawing.c -o drawing.ll` 会生成 IR，汇编约束可能为 `=r,r,r,~{dirflag},~{fpsr},~{flags}`。此时直接将 `drawing.ll` 交给本编译器；不要让 Clang 继续把这些模板汇编成 x86/ARM 机器目标文件，因为官方 Scratch opcode 不是那些处理器的指令。

## 已支持的 opcode 与参数

下表 `()` 内按顺序列出输入，方括号列出静态文本字段。所有列出的输入和字段都必须提供；没有列出的默认字段不会自行补全。

| 分类 | Opcode 与绑定 |
|---|---|
| 算术 | `operator_add/subtract/multiply/divide/mod(NUM1, NUM2)`；这里斜杠表示分别使用对应完整 opcode，例如 `operator_subtract` |
| 比较、布尔 | `operator_equals/lt/gt/and/or(OPERAND1, OPERAND2)`；`operator_not(OPERAND)` |
| 数学 | `operator_random(FROM, TO)`；`operator_round(NUM)`；`operator_mathop(NUM)[OPERATOR]` |
| 文本 | `operator_join(STRING1, STRING2)`；`operator_letter_of(LETTER, STRING)`；`operator_length(STRING)`；`operator_contains(STRING1, STRING2)` |
| 变量 | `data_variable()[VARIABLE]`；`data_setvariableto/changevariableby(VALUE)[VARIABLE]`；`data_showvariable/hidevariable()[VARIABLE]` |
| 列表读取 | `data_listcontents/lengthoflist()[LIST]`；`data_itemoflist(INDEX)[LIST]`；`data_itemnumoflist/listcontainsitem(ITEM)[LIST]` |
| 列表修改 | `data_addtolist(ITEM)[LIST]`；`data_deleteoflist(INDEX)[LIST]`；`data_insertatlist/replaceitemoflist(INDEX, ITEM)[LIST]`；`data_deletealloflist/showlist/hidelist()[LIST]` |
| 移动 | `motion_movesteps(STEPS)`；`motion_turnright/turnleft(DEGREES)`；`motion_gotoxy(X, Y)`；`motion_glidesecstoxy(SECS, X, Y)`；`motion_pointindirection(DIRECTION)` |
| 坐标 | `motion_changexby(DX)`；`motion_setx(X)`；`motion_changeyby(DY)`；`motion_sety(Y)`；`motion_xposition/yposition/direction()` |
| 移动设置 | `motion_ifonedgebounce()`；`motion_setrotationstyle()[STYLE]` |
| 外观、文字 | `looks_say/think(MESSAGE)`；`looks_sayforsecs/thinkforsecs(MESSAGE, SECS)`；`looks_show/hide/cleargraphiceffects()`；`looks_changesizeby(CHANGE)`；`looks_setsizeto(SIZE)`；`looks_size()` |
| 外观效果 | `looks_changeeffectby(CHANGE)[EFFECT]`；`looks_seteffectto(VALUE)[EFFECT]` |
| 侦测读取 | `sensing_timer/dayssince2000/mousex/mousey/mousedown/loudness/answer/username()`；`sensing_keypressed(KEY_OPTION)`；`sensing_current()[CURRENTMENU]` |
| 侦测命令 | `sensing_resettimer()`；`sensing_askandwait(QUESTION)` |
| 控制 | `control_if(CONDITION){SUBSTACK}`；`control_if_else(CONDITION){SUBSTACK,SUBSTACK2}`；`control_repeat(TIMES){SUBSTACK}`；`control_repeat_until(CONDITION){SUBSTACK}` |
| 等待 | `control_wait(DURATION)`；`control_wait_until(CONDITION)` |
| 终止 | `control_stop[STOP_OPTION]`，当前仅接受 `STOP_OPTION="all"`；用于运行时终止程序 |
| 画笔基本命令 | `pen_clear/stamp/penDown/penUp()`；`pen_setPenColorToColor(COLOR)` |
| 画笔参数 | `pen_changePenColorParamBy/setPenColorParamTo(COLOR_PARAM, VALUE)`；`pen_changePenSizeBy/setPenSizeTo(SIZE)` |
| 画笔旧兼容命令 | `pen_changePenHueBy/setPenHueToNumber(HUE)`；`pen_changePenShadeBy/setPenShadeToNumber(SHADE)` |

文本字段的允许值：

- `OPERATOR`：`abs`、`floor`、`ceiling`、`sqrt`、`sin`、`cos`、`tan`、`asin`、`acos`、`atan`、`ln`、`log`、`e ^`、`10 ^`。
- `STYLE`：`left-right`、`don't rotate`、`all around`。
- `EFFECT`：`COLOR`、`FISHEYE`、`WHIRL`、`PIXELATE`、`MOSAIC`、`BRIGHTNESS`、`GHOST`。
- `CURRENTMENU`：`YEAR`、`MONTH`、`DATE`、`DAYOFWEEK`、`HOUR`、`MINUTE`、`SECOND`。
- 画笔 `COLOR_PARAM` 输入的静态文本为 `color`、`saturation`、`brightness`、`transparency`；生成端将静态文本转换为官方菜单 shadow。动态输入保留 Scratch 自身行为。

当前未实现声音、资源选择、跨角色侦测、直接自定义积木调用、终止脚本、无限循环和其他未列出的 opcode。帽子、克隆和广播明确禁止。等待及询问命令会按 Scratch 原生行为让出执行或等待用户，不构成异步线程支持。渲染或交互依赖必须由实际 Scratch/TurboWarp 环境提供。

## 验证

`tests/assembly_test.cpp` 检查解析、约束、静态控制流、负值桥接及拒绝路径，并生成 `assembly-smoke.sb3`、`assembly-pen.sb3`。`tests/assembly_vm.cjs <输出目录>` 在原版 Scratch VM 与 TurboWarp 编译模式中执行它们。

数值用例覆盖负 `i32`、最小整数、`i8` 回绕、`i1`、布尔运算、负小数截断、重复输出赋值、变量与列表、循环、嵌套 reporter、NaN/无穷处理及输入快照。画笔用例检查扩展加载和积木运行，不声称验证实际像素；当前测试运行器没有 renderer。

`double` 桥接另覆盖 15 个确定性结果的 120 字节检查，包括正负零、分数、最大有限数、最小正规数、最小／最大次正规数及 NaN／无穷，并将实际天数 reporter 的 8 字节结果与宿主 binary64 表示逐字节对照。`tests/fixtures/asm_float.ll` 通过完整 LLVM→SB3 流水线验证位模式和日期 reporter；这些用例均在原版 VM 和 TW 编译模式执行。
