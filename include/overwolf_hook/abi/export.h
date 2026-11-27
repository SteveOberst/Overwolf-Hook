#pragma once
#if defined(OWH_BUILD_owh_client)
#define OWH_CLIENT __declspec(dllexport)
#else
#define OWH_CLIENT __declspec(dllimport)
#endif
