#ifndef __RECORDER_H__
#define __RECORDER_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "stdint.h"

typedef enum
{
    STATE_IDLE,
    STATE_RECORDING,
    STATE_BUSY,
    STATE_ERROR   
} recorder_state_t;


void rec_mainTask(void * pv);
void rec_startRecording(char* file_path);
void rec_stopRecording();
recorder_state_t rec_getState(uint16_t* time_ms);


#ifdef __cplusplus
}
#endif

#endif /* __RECORDER_H__ */