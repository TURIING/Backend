#pragma once

// do-while(0) 包裹保证宏按单条语句使用：不劫持调用处外层 if/else 的配对，宏内变量也不泄漏到调用方作用域
// 仅适用于无返回值语境（void 函数/构造函数/析构函数）
#define EARLY_RETURN(cond) \
    do {                   \
        if (cond) {        \
            return;        \
        }                  \
    } while (0)
