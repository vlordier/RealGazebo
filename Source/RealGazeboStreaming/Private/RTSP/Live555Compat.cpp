// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the GNU General Public License v3.0.
// See LICENSE file in the project root for full license information.

/**
 * Live555 Compatibility Layer for Unreal Engine
 *
 * This file provides wrapper functions for glibc symbols that Live555 libraries
 * depend on but are not directly available in Unreal Engine's build environment.
 *
 * The Live555 libraries were compiled with -D_FILE_OFFSET_BITS=64, which causes
 * glibc to redirect certain function calls (like fcntl) to their 64-bit variants
 * (like fcntl64). Unreal Engine's bundled toolchain doesn't automatically provide
 * these redirections, so we provide them manually here.
 *
 * NOTE: This compatibility layer is only needed on Linux platforms.
 * Windows uses different socket APIs and doesn't require fcntl compatibility.
 */

#if PLATFORM_LINUX

#include <fcntl.h>
#include <stdarg.h>

// Provide fcntl64 as a wrapper to fcntl
// This is needed because Live555 was compiled with _FILE_OFFSET_BITS=64
extern "C" int fcntl64(int fd, int cmd, ...)
{
	va_list args;
	va_start(args, cmd);

	// fcntl can take different types of arguments depending on the command
	// For most commands used by Live555, we pass a long argument
	void* arg = va_arg(args, void*);
	va_end(args);

	// Call the regular fcntl function
	// On 64-bit systems, fcntl and fcntl64 are typically the same
	return fcntl(fd, cmd, arg);
}

#endif // PLATFORM_LINUX
