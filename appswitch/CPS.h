// CPS.h

#pragma once

#include <ApplicationServices/ApplicationServices.h>


#ifdef __cplusplus
extern "C" {
#endif

#if PRAGMA_STRUCT_ALIGN
    #pragma options align=mac68k
#elif PRAGMA_STRUCT_PACKPUSH
    #pragma pack(push, 2)
#elif PRAGMA_STRUCT_PACK
    #pragma pack(2)
#endif


typedef ProcessSerialNumber CPSProcessSerNum;

extern OSErr	CPSPostHideMostReq( CPSProcessSerNum *psn);

extern OSErr	CPSPostShowAllReq( CPSProcessSerNum *psn);

#if PRAGMA_STRUCT_ALIGN
    #pragma options align=reset
#elif PRAGMA_STRUCT_PACKPUSH
    #pragma pack(pop)
#elif PRAGMA_STRUCT_PACK
    #pragma pack()
#endif

#ifdef __cplusplus
}
#endif
