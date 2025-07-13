#ifndef CBZ_PCH_H_
#define CBZ_PCH_H_

#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <array>
#include <memory>
#include <string>
#include <vector>

#include <algorithm>
#include <stdint.h>

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#ifndef CBZ_NULLABLE
#ifdef __clang__
#define CBZ_NULLABLE _Nullable
#else
#define CBZ_NULLABLE
#endif
#endif

#ifndef CBZ_NOT_NULL
#ifdef __clang__
#define CBZ_NOT_NULL _Nonnull
#else
#define CBZ_NOT_NULL
#endif
#endif

#ifndef CBZ_NO_DISCARD
#ifdef __clang__
#define CBZ_NO_DISCARD [[nodiscard]]
#else
#define CBZ_NO_DISCARD
#endif
#endif

#ifdef CBZ_EXPORTS
#ifdef CBZ_WIN32
#define CBZ_API __declspec(dllexport)
#else
#define CBZ_API
#endif
#else
#define CBZ_API
#endif

#if defined(_MSC_VER)
#define CBZ_NO_VTABLE __declspec(novtable)
#else
#define CBZ_NO_VTABLE
#endif

namespace cbz {

enum class Result {
  eSuccess = 0,
  eFailure = 1,

  eFileError,

  eGLFWError,
  eWGPUError,
  eSlangError,

  eNetworkFailure,
};

}

#endif
