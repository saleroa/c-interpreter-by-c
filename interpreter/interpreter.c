#include<stdio.h>
#include<stdlib.h>
#include<memory.h>
#include<string.h>
#include <fcntl.h>  // open 函数
#include <unistd.h> // read 和 close 函数


// 涉及到 int 和 指针 的强转
// 64 位机的地址是 64 位，所以如果是 64 位， 定义 int 位 64 位的 longlong int
#if __x86_64__
	#define int long long
#endif

// debug 模式
// #define DEBUG

#ifdef DEBUG
    #define PrintToken(t) printf("the token is : %lld\n",t);
#else
    #define PrintToken(t)
#endif


int token;    // current token
char *src, *old_src;  // pointer to source code
int poolsize; // default size of text/data/stack
int line;     // line number

// text 是解析出来的汇编代码
int *text, *old_text, *stack;
char * data;  // data 部分只用来放字符串

int *pc, *sp, *bp, ax, cycle; // registers 
// PC 是程序计数器， sp 是 stack pointer， bp 用来分割函数调用时的保留现场和子函数的变量
// ax 是可用的普通 register

// virtual machine instructions
enum { LEA ,IMM ,JMP ,CALL,JZ  ,JNZ ,ENT ,ADJ ,LEV ,LI  ,LC  ,SI  ,SC  ,PUSH,  // assemble
       OR  ,XOR ,AND ,EQ  ,NE  ,LT  ,GT  ,LE  ,GE  ,SHL ,SHR ,ADD ,SUB ,MUL ,DIV ,MOD ,
       OPEN,READ,CLOS,PRTF,MALC,MSET,MCMP,EXIT };

// 词法解析需要处理的  标识符/identifier
enum {
  Num = 128, Fun, Sys, Glo, Loc, Id,
  Char, Else, Enum, If, Int, Return, Sizeof, While,

  // 运算符，优先级由低到高
  Assign, Cond, Lor, Lan, Or, Xor, And, Eq, Ne, Lt, Gt, Le, Ge, Shl, Shr, Add, Sub, Mul, Div, Mod, Inc, Dec, Brak
};

// identifier/标识符 的字段
enum{Token,Hash,Name,Type,Class,Value,BType,BClass,BValue,  IdSize};

int token_val;  // 当前token的值, 主要用于数字 he 字符
int *current_id,  // 当前  标识符/identifier
    *symbols;   // 标志符/identifier 表

int *idmain; // 'main'
enum {CHAR, INT, PTR}; // 函数和变量的类型

// 
int basetype;
int expr_type;

// function frame
//
// 0: arg 1
// 1: arg 2
// 2: arg 3
// 3: return address
// 4: old bp pointer  <- index_of_bp
// 5: local var 1
// 6: local var 2
// bp 指针是函数调用时的，保存的现场和新函数的内部的隔离

int index_of_bp; // 当前 bp 的位置


// 词法分析器
// 调用一次就读取源代码将下一个需要解析的 token 写入 token 字段
void next(){
    char *last_pos;  // 当前解析的 token 的起始位置
    int hash;  // identifier 的 hash 值，用于比较
    
    // 遍历源代码，逐个找到需要处理的 标识符

    while(token = *src){ // 判断 token 的 ascii 码是否为 0， \0 对应 ascii 为 0，表示777777777777777777777777777777777777777结束
        // 全是 if ，跳过 空格
        
        ++src; // src 是 token 的前一位

        // parse token

        // 换行符
        if(token == '\n'){    
            ++line;
        }
        // 宏,不支持,直接跳过
        else if(token == '#'){    
            while(*src != 0 && *src != '\n'){
                src++;
            }
        }
        
        // 解析关键字，以及变量名， token 都是 id
        else if((token >= 'a' && token <= 'z') || (token >= 'A' && token <= 'Z') || (token == '_')){
            // parse identifier
            last_pos = src -1;
            hash = token;

            while((*src >= 'a' && *src <= 'z') || (*src >= 'A' && *src <= 'Z') || (*src >= '0' && *src <= '9' )|| (*src == '_')){
                hash = hash * 147 + *src; // 147 是用来计算 hash 的一个常用质数
                src++;
            }

            // 从 标识/identifier 表 symbols 的第一个元素开始，逐个匹配
            current_id = symbols;
            while(current_id[Token]){
                if(current_id[Hash] == hash && !memcmp((char*)current_id[Name],last_pos,src-last_pos)){
                    // 找到了对应的标识，返回 token
                    token = current_id[Token];
                    PrintToken(token);
                    return;
                }
                current_id = current_id + IdSize;
            }

            // 储存新的 标识/identifier 进入 symbols 标识表
            current_id[Name] = (int)last_pos;
            current_id[Hash] = hash;
            token = current_id[Token] = Id;


            // 找到新的 Id 标识符，退出next函数
            break;
        }

        // 数字
        else if(token >= '0' && token <= '9'){
            // 解析数数字
            // 三种类型， 十进制 123，十六进制 0x123，八进制 017
            token_val = token - '0';
            if(token_val > 0){
                // 不是 0 打头，十进制
                while(*src >= '0' && *src <= '9'){
                    token_val = token_val * 10 + *src++ - '0';
                }
            }else{
                // 0 打头
                if(*src == 'x' || *src == 'X'){
                    // 十六进制
                    token = *++src;
                    while((token >= '0' && token <= '9') || (token >= 'a' && token <= 'f')|| (token >= 'A' && token <= 'F')){
                        // 第一次循环的时候，token_val 是 0
                        // 对应的 ascii : a 97, A 65
                        token_val = token_val * 16 + (token & 15) + (token >= 'A' ? 9 : 0);
                        token = *++src;
                    }
                }else{
                    // 八进制
                    while(*src >= '0' && *src <= '7'){
                        token_val = token_val * 8 + *src++ - '0';
                    }
                }
            }
            token = Num;
            break;
        }

        // 字符串
        else if(token == '"' || token == '\''){
            // 字符串会放入 data 内存部分
            last_pos = data;

            // 循环直到字符串结束或遇到匹配的引号
            while(*src != 0 && *src != token){
                token_val = *src++;
                if(token_val == '\\'){
                    token_val = *src++;
                    if (token_val == 'n'){
                        token_val = '\n';
                    }
                }

                // 如果是 " 开头，就是字符串，将字符串的字符一个一个填入 data 区域
                if(token == '"'){
                    *data++ = token_val;
                }
            }
            src++;
            
            // 如果是 '' 包裹的单个字符，当作数字处理，token 就是 Num，token_value 就是 值
            // 字符串返回的 token 是 " ,token_val 是字符串地址
            if(token == '"'){ 
                token_val = (int)last_pos;
            }else{
                token = Num;
            }
            break;
        }

        // 注释， 仅支持 // 注释, 不支持 /* comment */
        else if(token == '/'){
            if(*src == '/'){
                // 跳过注释部分
                while(*src != 0 && *src != '\n'){
                    ++src;
                }
            }else{
                // 只有一个 / 就是除法
                token = Div;
                break;
            }
        }

        // 其他
        else if( token == '='){
            // 解析 == 和 =
            if(*src == '='){
                src++;
                token = Eq;
            }else{
                token = Assign;
            }
            break;
        }else if(token == '+'){
            // 解析 + 和 ++
            if(*src == '+'){
                src++;
                token = Inc;
            }else{
                token = Add;
            }
            break;
        }else if(token == '-'){
            // 解析 - 和 --
            if(*src == '-'){
                src++;
                token = Dec;
            }else{
                token = Sub;
            }
            break;
        }else if(token == '!'){
            // 解析 ！=
            if(*src == '='){
                src++;
                token = Ne;
            }
            break;
        }else if(token == '<'){
            // 解析 << 和 <= 和 <
            if(*src == '='){
                src++;
                token = Le;
            }else if(*src == '<'){
                src++;
                token = Shl;
            }else{
                token = Lt;
            }
            break;
        }else if(token == '>'){
            // 解析 >> 和 >= 和 >
            if(*src == '='){
                src++;
                token = Ge;
            }else if(*src == '>'){
                src++;
                token = Shr;
            }else{
                token = Gt;
            }
            break;
        }else if(token == '|'){
            // 解析 | 和 ||
            if(*src == '|'){
                src++;
                token = Lor;
            }else{
                token = Or;
            }
            break;
        }else if(token == '&'){
            // 解析 & 和 &&
            if(*src == '&'){
                src++;
                token = Lan;
            }else{
                token = And;
            }
        }else if(token == '^'){
            token = Xor;
            break;
        }else if(token == '%'){
            token = Mod;
            break;
        }else if(token == '*'){
            token = Mul;
            break;
        }else if(token == '['){
            token = Brak;
            break;
        }else if(token == '?'){
            token = Cond;
            break;
        }else if(token == '~' || token == ';' || token == '{' || token == '}' || token == '(' || token == ')' || token == ']' || token == ',' || token  == ':'){
            return;
        }

    }

    // 防止 token 输出为字符串结尾的 "
    if(token != '"')  PrintToken(token);
    return;
}



// 封装 next 函数
void match(int tk){
    if(token == tk){
        next();
    }else{
        printf("%lld: expectd token :%lld \n",line,tk);
        exit(-1);
    }
}


void expression(int level) {
    int *id;
    int tmp;
    int *addr;
    
    // 先处理一元运算 
    {
        if (!token) {
            printf("%lld: unexpected token EOF of expression\n", line);
            exit(-1);
        }

        // token 为 num 的时候， token_val 是对应的值
        if (token == Num) {
            match(Num);

            // emit code
            *++text = IMM;
            *++text = token_val;
            expr_type = INT;
        }

        // token 为 “ 的时候， 意味着是 string 
        // token_val 就是字符串的地址 
        else if (token == '"') {
            // c 语言语法支持两段连接的 string， "abc" "abc"

            // emit code
            *++text = IMM;
            *++text = token_val;

            match('"');
            // 处理连接的 string
            while (token == '"') {
                match('"');
            }

            // data 指向 string 的最后一个位置，后移一位，因为data一开始都被置零，空出一个 \0 表示结束符。
            // & (-sizeof(int) 是用来对齐内存
            data = (char *)(((int)data + sizeof(int)) & (-sizeof(int)));
            expr_type = PTR;
        }

        // 内置的 sizeof 函数
        else if (token == Sizeof) {
            // 仅支持 sizeof(int) sizeof(int)  sizeof(ptr) 
            match(Sizeof);
            match('(');
            expr_type = INT;

            if (token == Int) {
                match(Int);
            } else if (token == Char) {
                match(Char);
                expr_type = CHAR;
            }

            while (token == Mul) {
                match(Mul);
                expr_type = expr_type + PTR;
            }

            match(')');

            // emit code
            *++text = IMM;
            *++text = (expr_type == CHAR) ? sizeof(char) : sizeof(int);

            // 最终类型是 int
            expr_type = INT;
        }

        // 定义过的 id
        else if (token == Id) {
            // 该项目中只有三种可能
            // 1. 函数调用
            // 2. enum 变量，类型为 num
            // 3. 局部变量 / 全局变量
            match(Id);

            id = current_id;

            if (token == '(') {
                // 函数调用
                match('(');

                // 参数传递
                tmp = 0; // 参数个数
                while (token != ')') {
                    expression(Assign);
                    *++text = PUSH;
                    tmp ++;

                    if (token == ',') {
                        match(',');
                    }

                }
                match(')');

                // emit code
                if (id[Class] == Sys) {
                    // 系统函数
                    *++text = id[Value];
                }
                else if (id[Class] == Fun) {
                    // 函数调用
                    *++text = CALL;
                    *++text = id[Value];
                }
                else {
                    printf("%lld: bad function call\n", line);
                    exit(-1);
                }

                // 函数参数退栈
                if (tmp > 0) {
                    *++text = ADJ;
                    *++text = tmp;
                }
                expr_type = id[Type];
            }
            else if (id[Class] == Num) {
                // enum
                *++text = IMM;
                *++text = id[Value];
                expr_type = INT;
            }
            else {
                // 变量
                if (id[Class] == Loc) {
                    *++text = LEA;
                    *++text = index_of_bp - id[Value];
                }
                else if (id[Class] == Glo) {
                    *++text = IMM;
                    *++text = id[Value];
                }
                else {
                    printf("%lld: undefined variable\n", line);
                    exit(-1);
                }

                // emit code
                // 所有变量都是通过地址，然后加载值
                expr_type = id[Type];
                *++text = (expr_type == Char) ? LC : LI;
            }
        }

        // 括号
        else if (token == '(') {
            // 类型转换 或者 普通括号
            match('(');
            if (token == Int || token == Char) {
                tmp = (token == Char) ? CHAR : INT; // cast type
                match(token);
                while (token == Mul) {
                    match(Mul);
                    tmp = tmp + PTR;
                }

                match(')');

                expression(Inc); // 类型转换的优先级和 inc 自增一样

                expr_type  = tmp;
            } else {
                // normal parenthesis
                expression(Assign);
                match(')');
            }
        }

        // 由指针取值
        else if (token == Mul) {
            // dereference *<addr>
            match(Mul);
            expression(Inc); // 指针取值 和 取地址 的优先级和 inc 自增一样

            if (expr_type >= PTR) {
                expr_type = expr_type - PTR;
            } else {
                printf("%lld: bad dereference\n", line);
                exit(-1);
            }

            *++text = (expr_type == CHAR) ? LC : LI;
        }

        // 对变量取地址
        else if (token == And) {
            match(And);
            expression(Inc); 
            // 所有变量的值都是从地址通过 lc 或者 li 获取
            // 直接去除 lc 或者 li
            if (*text == LC || *text == LI) {
                text --;
            } else {
                printf("%lld: bad address of\n", line);
                exit(-1);
            }

            expr_type = expr_type + PTR;
        }

        // 逻辑取反
        else if (token == '!') {
            // not
            match('!');
            expression(Inc);

            // emit code,
            // <expr> == 0，如果为 0 则 1， 为 1 则 0
            // 将 ax 压入栈，0 放入 ax，对比
            *++text = PUSH;
            *++text = IMM;
            *++text = 0;
            *++text = EQ;

            expr_type = INT;
        }

        // 按位取反 
        else if (token == '~') {
            // bitwise not
            match('~');
            expression(Inc);

            // emit code, use <expr> XOR -1
            // 将 ax 压入栈，-1 放入 ax， xor
            *++text = PUSH;
            *++text = IMM;
            *++text = -1;
            *++text = XOR;

            expr_type = INT;
        }

        // 正号
        else if (token == Add) {
            // +var, do nothing
            match(Add);
            expression(Inc);

            expr_type = INT;
        }

        // 负号
        else if (token == Sub) {
            // -var
            match(Sub);

            if (token == Num) {
                // 对数字取负，直接 -(NUM)
                *++text = IMM;
                *++text = -token_val;
                match(Num);
            } else {
                // 对变量取负，-1 * a
                *++text = IMM;
                *++text = -1;
                *++text = PUSH;
                expression(Inc);
                *++text = MUL;
            }

            expr_type = INT;
        }

        // 前缀自增自减
        else if (token == Inc || token == Dec) {
            // 画内存图研究
            tmp = token;
            match(token);
            expression(Inc);
            if (*text == LC) {
                *text = PUSH;
                *++text = LC;
            } else if (*text == LI) {
                *text = PUSH;
                *++text = LI;
            } else {
                printf("%lld: bad lvalue of pre-increment\n", line);
                exit(-1);
            }
            *++text = PUSH;
            *++text = IMM;
            // 有可能是指针自增自减，也有可能是数字变量自增自减
            *++text = (expr_type > PTR) ? sizeof(int) : sizeof(char);
            *++text = (tmp == Inc) ? ADD : SUB;
            *++text = (expr_type == CHAR) ? SC : SI;
        }
        else {
            printf("%lld: bad expression\n", line);
            exit(-1);
        }
    }

    // 二元运算符 和 前置运算符号
    {
        // level 是外部运算，如果 token 的运算更优先就递归先计算 token 对应的运算

        // = 等号
        while (token >= level) {
            // handle according to current operator's precedence
            tmp = expr_type;
            if (token == Assign) {
                // var = expr;
                match(Assign);
                if (*text == LC || *text == LI) {
                    *text = PUSH; // save the lvalue's pointer
                } else {
                    printf("%lld: bad lvalue in assignment\n", line);
                    exit(-1);
                }
                expression(Assign);

                expr_type = tmp;
                *++text = (expr_type == CHAR) ? SC : SI;
            }

            // ? : 三目运算符
            else if (token == Cond) {
                // expr ? a : b;
                // 流程逻辑类似 if，代码和 if 一样
                match(Cond);
                *++text = JZ;
                addr = ++text;
                expression(Assign);
                if (token == ':') {
                    match(':');
                } else {
                    printf("%lld: missing colon in conditional\n", line);
                    exit(-1);
                }
                *addr = (int)(text + 3);
                *++text = JMP;
                addr = ++text;
                expression(Cond);
                *addr = (int)(text + 1);
            }

            // 对于 LOR 和 LAN
            /* 
            <expr1> || <expr2>     <expr1> && <expr2>
            ...<expr1>...          ...<expr1>...
                JNZ b                  JZ b
            ...<expr2>...          ...<expr2>...
            b:                     b:
            */
            else if (token == Lor) {
                // logic or
                match(Lor);
                *++text = JNZ;
                addr = ++text;
                expression(Lan);
                *addr = (int)(text + 1);
                expr_type = INT;
            }
            else if (token == Lan) {
                // logic and
                match(Lan);
                *++text = JZ;
                addr = ++text;
                expression(Or);
                *addr = (int)(text + 1);
                expr_type = INT;
            }

            // 数学运算
            /* 
            <expr1> ^ <expr2>
            ...<expr1>...          <- now the result is on ax
            PUSH
            ...<expr2>...          <- now the value of <expr2> is on ax
            XOR
            */
            else if (token == Or) {
                // bitwise or
                match(Or);
                *++text = PUSH;
                // expression 内部的 expression 参数
                // 会挑选比当前 token 优先级更高一级的运算符
                expression(Xor);
                *++text = OR;
                expr_type = INT;
            }
            else if (token == Xor) {
                // bitwise xor
                match(Xor);
                *++text = PUSH;
                expression(And);
                *++text = XOR;
                expr_type = INT;
            }
            else if (token == And) {
                // bitwise and
                match(And);
                *++text = PUSH;
                expression(Eq);
                *++text = AND;
                expr_type = INT;
            }
            else if (token == Eq) {
                // equal ==
                match(Eq);
                *++text = PUSH;
                expression(Ne);
                *++text = EQ;
                expr_type = INT;
            }
            else if (token == Ne) {
                // not equal !=
                match(Ne);
                *++text = PUSH;
                expression(Lt);
                *++text = NE;
                expr_type = INT;
            }
            else if (token == Lt) {
                // less than
                match(Lt);
                *++text = PUSH;
                expression(Shl);
                *++text = LT;
                expr_type = INT;
            }
            else if (token == Gt) {
                // greater than
                match(Gt);
                *++text = PUSH;
                expression(Shl);
                *++text = GT;
                expr_type = INT;
            }
            else if (token == Le) {
                // less than or equal to
                match(Le);
                *++text = PUSH;
                expression(Shl);
                *++text = LE;
                expr_type = INT;
            }
            else if (token == Ge) {
                // greater than or equal to
                match(Ge);
                *++text = PUSH;
                expression(Shl);
                *++text = GE;
                expr_type = INT;
            }
            else if (token == Shl) {
                // shift left
                match(Shl);
                *++text = PUSH;
                expression(Add);
                *++text = SHL;
                expr_type = INT;
            }
            else if (token == Shr) {
                // shift right
                match(Shr);
                *++text = PUSH;
                expression(Add);
                *++text = SHR;
                expr_type = INT;
            }

            else if (token == Add) {
                // add
                match(Add);
                *++text = PUSH;
                expression(Mul);

                expr_type = tmp;
                if (expr_type > PTR) {
                    // 因为 CHAR = 0 
                    // 指针的加法，并且不是 char * 指针
                    *++text = PUSH;
                    *++text = IMM;
                    *++text = sizeof(int);
                    *++text = MUL;
                }
                *++text = ADD;
            }
            else if (token == Sub) {
                // sub
                match(Sub);
                *++text = PUSH;
                expression(Mul);
                
               
                if (tmp > PTR && tmp == expr_type) {
                    // 指针是 * int 类型
                    // 两个指针相减，值就是两个指针差的基础类型的 *int 的距离
                    *++text = SUB;
                    *++text = PUSH;
                    *++text = IMM;
                    *++text = sizeof(int);
                    *++text = DIV;
                    expr_type = INT;
                } else if (tmp > PTR) {
                    // pointer movement
                    *++text = PUSH;
                    *++text = IMM;
                    *++text = sizeof(int);
                    *++text = MUL;
                    *++text = SUB;
                    expr_type = tmp;
                } else {
                    // numeral subtraction
                    *++text = SUB;
                    expr_type = tmp;
                }
            }
            else if (token == Mul) {
                // multiply
                match(Mul);
                *++text = PUSH;
                expression(Inc);
                *++text = MUL;
                expr_type = tmp;
            }
            else if (token == Div) {
                // divide
                match(Div);
                *++text = PUSH;
                expression(Inc);
                *++text = DIV;
                expr_type = tmp;
            }
            else if (token == Mod) {
                // Modulo
                match(Mod);
                *++text = PUSH;
                expression(Inc);
                *++text = MOD;
                expr_type = tmp;
            }
            else if (token == Inc || token == Dec) {
                // 后缀自增自减， x++
                // 相比前缀，ax 仍保留 x 的原值
                if (*text == LI) {
                    *text = PUSH;
                    *++text = LI;
                }
                else if (*text == LC) {
                    *text = PUSH;
                    *++text = LC;
                }
                else {
                    printf("%lld: bad value in increment\n", line);
                    exit(-1);
                }

                // 前半部分和前缀相同
                *++text = PUSH;
                *++text = IMM;
                *++text = (expr_type > PTR) ? sizeof(int) : sizeof(char);
                *++text = (token == Inc) ? ADD : SUB;
                *++text = (expr_type == CHAR) ? SC : SI;

                // ax 已经是 x++ ，后半部分做相反的操作 
                *++text = PUSH;
                *++text = IMM;
                *++text = (expr_type > PTR) ? sizeof(int) : sizeof(char);
                *++text = (token == Inc) ? SUB : ADD;
                match(token);
            }

            // 数组取值
            // a[10] = *(a+10)
            else if (token == Brak) {
                // array access var[xx]
                match(Brak);
                *++text = PUSH;
                expression(Assign);
                match(']');

                if (tmp > PTR) {
                    // 指针，不是 char* 
                    *++text = PUSH;
                    *++text = IMM;
                    *++text = sizeof(int);
                    *++text = MUL;
                }
                else if (tmp < PTR) {
                    printf("%lld: pointer type expected\n", line);
                    exit(-1);
                }
                expr_type = tmp - PTR;
                *++text = ADD;
                *++text = (expr_type == CHAR) ? LC : LI;
            }
            else {
                printf("%lld: compiler error, token = %lld\n", line, token);
                exit(-1);
            }
        }
    }
}


// 语句分析
void statement(){
    // statement 有以下几种
    // 1. if (...) <statement> [else <statement>]
    // 2. while (...) <statement>
    // 3. { <statement> }
    // 4. return xxx;
    // 5. <empty statement>;
    // 6. expression;  以分号结尾


    int * a, *b;  // 用于分支控制

    // if 和 while 的汇编可以画图理解

    if(token == If){
        // if (...) <statement> [else <statement>]
        //
        //   <cond>
        //   JZ a
        //   <true_statement>
        //   JMP b
        // a:
        //    <false_statement>
        // b:      
        //
        match(If);
        match('(');
        expression(Assign); // 解析条件
        match(')');

        *++text = JZ;  // text 是汇编代码的指针
        b = ++text;
        
        statement();  //

        if(token == Else){
            match(Else);

            *b = (int)(text + 3);
            *++text = JMP;
            b = ++text;
            
            statement();
        }

        *b = (int)(text + 1);
    }

    else if(token == While){
        // a:
        //      <cond>
        // JZ b
        //      <statement>
        //      JMP a
        // b
        match(While);

        a = text + 1;

        match('(');
        expression(Assign);
        match(')');

        *++text = JZ;
        b = ++text;

        statement();

        *++text = JMP;
        *++text = (int)a;
        *b = (int)(text + 1);
    }
    
    else if(token == '{'){
        match('{');
        while(token != '}'){
            statement();
        }
        match('}');
    }

    else if(token == Return){
        match(Return);
        if(token != ';'){
            expression(Assign);
        }
        match(';');

        // 
        *++text = LEV;
    }

    else if(token == ';'){
        // 空的 statement
        match(';');
    }

    else{
        expression(Assign);
        match(';');
    }
}


// 解析函数参数
void function_parameter(){
    int type;
    int params;
    params = 0;

   
    while(token != ')'){
        type = INT;
        if(token == Int){
            match(Int);
        }else if(token == Char){
            type = CHAR;
            match(Char);
        }

        while(token == Mul){
            match(Mul);
            type = type + PTR;
        }

        // 参数名字
        if(token != Id){
            printf("%lld: bad parameter declaration\n", line);
            exit(-1);
        }

        // 因为 next 函数
        // id 要么是新的，要么是旧的相同 id
        if (current_id[Class] == Loc) {
            printf("%lld: duplicate parameter declaration\n", line);
            exit(-1);
        }

        match(Id);

        // 无论全局变量 id 是否存在，将之前的值存入指定 B 字段，然后给新字段赋值
        current_id[BClass] = current_id[Class]; current_id[Class] = Loc;
        current_id[BType] = current_id[Type]; current_id[Type] = type;
        current_id[BValue] = current_id[Value];  current_id[Value] = params++;

        if(token == ','){
            match(',');
        } 
    }
    
    index_of_bp = params + 1;
}


void function_body(){
    int pos_local; // 当前参数该放的位置
    int type;
    pos_local = index_of_bp;

    // 这个循环保证每一行声明都会被读取到
    while(token == Int || token == Char){
        basetype = (token == Int) ? INT : CHAR;
        match(token);

        // 这个 while 将一行声明读取结束，直到 ;
        while(token != ';'){
            type = basetype;
            while(token == Mul){
                match(Mul);
                type = type + PTR;
            }
            
            if (token != Id) {
                // invalid declaration
                printf("%lld: bad local declaration\n", line);
                exit(-1);
            }
            if(current_id[Class] == Loc){
                // 已经存在局部变量
                printf("%lld: duplicate local declaration\n", line);
                exit(-1);
            }
            match(Id);

            // 存储局部变量
            // store the local variable
            current_id[BClass] = current_id[Class]; current_id[Class]  = Loc;
            current_id[BType]  = current_id[Type];  current_id[Type]   = type;
            current_id[BValue] = current_id[Value]; current_id[Value]  = ++pos_local;   // index of current parameter
            
            if(token == ','){
                match(',');
            }
        }
        match(';');
    }

    // 在栈上给局部变量留空间
    *++text = ENT;
    *++text = pos_local - index_of_bp;

    // 解析函数语句
    while(token != '}'){
        statement();
    }

    // 退出函数的 指令
    *++text = LEV;
}


void function_declaration(){
    match('(');
    function_parameter();
    match(')');
    match('{');
    function_body();
    // 函数解析放在 global_declaration 里，需要检测 } 来结束函数
    // 所以这里留下 } 不消耗
    // match('}');  

    // 函数里的函数
    current_id = symbols;
    while(current_id[Token]){
        if(current_id[Class] == Loc){
            current_id[Class] = current_id[BClass];
            current_id[Type] = current_id[BType];
            current_id[Value] = current_id[BValue];
        }
        current_id = current_id + IdSize;
    }
}


// 解析 enum 
void enum_declaration(){
    // eg. enum [id] {a = 1, b =3 ..}
    int i;
    i = 0;

    while(token != '}'){   // 判断 enum 是否结束
        if (token != Id){
            printf("%lld: bad enum identifier %lld\n",line,token);  
            exit(-1);
        }
        next();
        if(token == Assign){
            // eg. {a = 10}
            next();
            if(token != Num){
                printf("%lld: bad enum initializer \n",line);
                exit(-1);
            }
            i = token_val;
            next();
        }
        current_id[Class] = Num;
        current_id[Type] = INT;
        current_id[Value] = i++;

        if(token == ','){
            next();
        }  
    } 
}


void global_declaration(){
    int type; // 变量的实际类型
    int i; 

    basetype = INT;

    // 解析 enum
    if(token == Enum){
        // enum [id] {a =10, b =20 ..}
        match(Enum);
        if(token != '{'){
            match(Id); // 跳过 id 部分
        }
        
        if(token == '{'){
            // 解析指定部分
            match('{');
            enum_declaration();
            match('}');
        }
    
    match(';');
    return;
    }

    // 解析类型信息
    if(token == Int){
        match(Int);
    }else if(token == Char){
        match(Char);
        basetype= CHAR;
    }

    // 解析逗号分割的多个变量声明，或者是函数声明
    // eg. int a.b;  int func a() {}
    while(token != ';' && token != '}'){
        type = basetype;
        // * ，这里表示指针
        while(token == Mul){
            match(Mul);
            type = type + PTR;
        }

        if(token != Id){
            // 无效
            printf("%lld: bad global declaration \n",line);
            exit(-1);
        }

        if(current_id[Class]){
            // 该全局变量已经存在
            // 这里是全局变量检测，应该不存在局部变量
            printf("%lld: duplicate global declaration\n",line);
            exit(-1);
        }

        match(Id);
        current_id[Type] =  type;

        if(token == '('){
            // 确定是函数
            current_id[Class] = Fun;
            current_id[Value] = (int)(text+1);  // 用当前 text 地址当作函数地址
            function_declaration(); // 继续解析函数，将函数处理步骤写入后续的 text
        }else{
            // 确定是变量
            current_id[Class] = Glo; // 全局变量
            // 变量都是通过地址然后加载值
            current_id[Value] = (int)data; // 指定的内存地址
            data = data + sizeof(int);
        }

        if(token == ','){
            match(',');
        }
    }
    next();
}



// 用于语法分析，基于词法分析
void program(){
    next();
    while(token > 0){ // 即指针不为 null，也就是 source 没结束
        global_declaration();
    }
}

// 虚拟机的入口，当语法分析结束后执行
int eval(){
    // 遍历解析后 pc 指向的指令数组

    int op, *tmp;
    while(1){
        op = *pc++;  // pc 是 op 的后一个字段
        switch(op){

        // 汇编指令
            // mov 系列指令
            case IMM:  ax = *pc++; break;        // imm <value> 将 value 存入 ax
            case LC:   ax = *(char*)ax; break;   // LC 将对应地址中的字符载入 ax 中，要求 ax 中存放地址
            case LI:   ax = *(int*)ax; break;    // LI 将对应地址中的整数载入 ax 中，要求 ax 中存放地址
            // sp 指向栈顶的位置，栈顶放 地址，就可以将 ax 中的值存入地址中
            case SC:   *(char*)*sp++ = ax; break; // SC 将 ax 中的数据作为字符存放入地址中，要求栈顶存放地址
            case SI:   *(int*)*sp++ = ax; break; // SI 将 ax 中的数据作为整数存放入地址中，要求栈顶存放地址

            // PUSH
            case PUSH: *--sp = ax; break;         // 将 ax 压入栈顶

            // jump
            case JMP:  pc = (int *)*pc; break;    // jump <addr>, 将 pc 设置为指定的 addr
            case JZ:   pc = ax ? pc + 1 : (int*)*pc; break;  // 如果 ax == 0， jump
            case JNZ:  pc = ax ? (int*)*pc : pc + 1; break;  // 如果 ax != 0， jump

            // function calling
                // call <addr>
            case CALL: *--sp = (int)(pc+1); pc = (int*)*pc; break;  // 将返回地址压入栈， pc 设置为 addr
                // ent <size>
            case ENT:  *--sp = (int)bp; bp = sp; sp = sp - *pc++; break; // 保留当前栈指针，并创建栈指针下移 size 用于存放局部变量
                // adj <size>
            case ADJ:  sp = sp + *pc++; break;  // 栈指针上移 size ，将调用子函数时压入栈中的数据清除
            case LEV:  sp = bp; bp = (int*)*sp++; pc = (int*)*sp++; break;  // 恢复栈指针，并将 pc 设置为函数退出的位置
                // lea <offset> 子函数获取参数，看图
            case LEA:  ax  = (int)(bp + *pc++); break;
        
        // 运算符指令
            case OR:  ax = *sp++ | ax; break;    // 栈顶数据在前，ax 在后的计算，结果存入 ax 中
            case XOR:  ax = *sp++ ^ ax; break;
            case AND:  ax = *sp++ & ax; break;
            case EQ:  ax = *sp++ == ax; break;
            case NE:  ax = *sp++ != ax; break;
            case LT:  ax = *sp++ < ax; break;
            case LE:  ax = *sp++ <= ax; break;
            case GT:  ax = *sp++ > ax; break;
            case GE:  ax = *sp++ >= ax; break;
            case SHL:  ax = *sp++ << ax; break;
            case SHR:  ax = *sp++ >> ax; break;
            case ADD:  ax = *sp++ + ax; break;
            case SUB:  ax = *sp++ - ax; break;
            case MUL:  ax = *sp++ * ax; break;
            case DIV:  ax = *sp++ / ax; break;
            case MOD:  ax = *sp++ % ax; break;

        // 内置函数
            // 函数参数在栈中是从上储存到下
            case EXIT: printf("exit(%lld)",*sp); return *sp; break;
            case OPEN: ax = open((char*)sp[1],sp[0]); break;
            case CLOS: ax = close(sp[0]); break;
            case READ: ax = read(sp[2],(char*)sp[1],sp[0]); break;
            // prinf ？？？
            case PRTF: tmp = sp + pc[1]; ax = printf((char *)tmp[-1], tmp[-2], tmp[-3], tmp[-4], tmp[-5], tmp[-6]); break;
            case MALC: ax = (int)malloc(sp[0]); break;
            case MSET: ax = (int)memset((char*)sp[2],sp[1],sp[0]); break;
            case MCMP: ax = memcmp((char*)sp[2],(char*)sp[1],sp[0]); break;
            default:  printf("unknown instruction: %lld \n",op); return -1; break;
        }
    }
    return 0;
}


// eg. ./interpreter a.c
int main(int argc, char ** argv){

    int i;  // 记录读取字符长度
    int fd; // 源文件文件描述符
    int *tmp;

    argc--; argv++; // 去除第一个参数，./interpreter

    poolsize = 256 * 1024;  // 大小可变

    line = 1; // 

        // 打开源码文件
    if((fd = open(*argv,0)) < 0){
        printf("could not open (%s) \n",*argv);
        return -1;
    }


    // 为虚拟机分配内存
    
    // 分配 text 区域
    if(!(text = old_text = (int*)malloc(poolsize))){
        printf("could not malloc (%lld) for text area \n",poolsize);
        return -1;
    }
    
    // 分配 data 区域
    if(!(data = (char*)malloc(poolsize))){
        printf("could not malloc (%lld) for data area \n",poolsize);
        return -1;
    }
    
    // 分配 stack 区域
    if(!(stack = (int*)malloc(poolsize))){
        printf("could not malloc (%lld) for stack area \n",poolsize);
        return -1;
    }

    // 分配 symbols 符号表
    if(!(symbols = (int*)malloc(poolsize))){
        printf("could not malloc (%lld) for symbol table area \n",poolsize);
        return -1;
    }

    // 将分配的内存都置零
    memset(text,0,poolsize);
    memset(data,0,poolsize);
    memset(stack,0,poolsize);
    memset(symbols,0,poolsize);

    // SP he BP 都指向栈顶
    bp = sp = (int*) ((int)stack + poolsize);
    ax = 0;

    // 向 符号表/symbol 中添加元素
        // 添加关键字
    i = Char;
    src = "char else enum if int return sizeof while "
          "open read close printf malloc memset memcmp exit void main";
    while(i <= While){
        next();
        current_id[Token] = i++;
    }
        // 添加库函数
    i = OPEN;
    while(i <= EXIT){
        next();
        current_id[Class] = Sys;
        current_id[Type] = INT;
        current_id[Value] = i++;
    }

    // 先添加了 void 和 main 的关键字在 symbol table 中
    next(); 
    current_id[Token] = Char;
    next();
    idmain = current_id; 

    #ifdef DEBUG
    // 内置 identifier 设置完毕
        printf("\nBuilt-in identifier setup complete \n\n");
    #endif
    
    // 为读取到的代码配内存
    if( !(src = old_src = (char*)malloc(poolsize)) ){
        printf("failed to malloc (%lld) for source area \n",poolsize);
        return -1;
    }

    if((i = read(fd,src,poolsize-1)) <= 0){
        printf("read() returned %lld \n" , i);
        return -1;
    }
    src[i] = 0; // 源码加一个 EOF
    close(fd);  // 关闭文件


    program();  // 开始分析



    #ifdef DEBUG
        printf("\nparse token done\n\n");
    #endif

    if (!(pc = (int *)idmain[Value])) {
        printf("main() not defined\n");
        return -1;
    }

    // setup stack
    sp = (int *)((int)stack + poolsize);
    *--sp = EXIT; // call exit if main returns
    *--sp = PUSH; tmp = sp;
    *--sp = argc;
    *--sp = (int)argv;
    *--sp = (int)tmp;

    
    return eval(); // 分析结束后，虚拟机执行
} 