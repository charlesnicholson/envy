#pragma once

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <io.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#endif
