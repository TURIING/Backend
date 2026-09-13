#pragma once

// Backend 命名空间宏。
//
// 单独成文件是为了打破 DriverDefine.h 与 Handle.h 的循环包含：Handle.h 只需要这几个宏，
// 却曾为此包含整个 DriverDefine.h，而后者又要包含 Handle.h 才能引用句柄类型。
#define BEGIN_NS_BACKEND namespace Backend {
#define END_NS_BACKEND   }
#define NS_BD            Backend
