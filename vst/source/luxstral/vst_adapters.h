/*
 * vst_adapters.h
 *
 * Adaptation layer for LuxStral engine in VST context
 * C++ wrapper around vst_adapters_c.h
 *
 * Author: zhonx
 * Created: January 2026
 */

#ifndef __VST_ADAPTERS_H__
#define __VST_ADAPTERS_H__

/* Include C-compatible definitions */
#include "vst_adapters_c.h"

/* C++ specific additions ----------------------------------------*/
#ifdef __cplusplus

// C++ only: bool return type for buffer ready check
extern "C" bool luxstral_are_audio_buffers_ready(void);

#endif /* __cplusplus */

#endif /* __VST_ADAPTERS_H__ */
