#ifndef FANCTRL_H_INCLUDED
  #define FANCTRL_H_INCLUDED

#define FANCTRL_VERSION_MAJOR  0
#define FANCTRL_VERSION_MINOR  1
#define FANCTRL_VERSION_PATCH  0

enum
{
  /* Return Codes from fanCtrl_Run */
  RUN_RET_OK=0,
  RUN_RET_ERR_INIT,
  RUN_RET_ERR_SENSOR_READ,
  RUN_RET_ERR_FAN_ENABLE,
  RUN_RET_ERR_PWM_WRITE,

  /* Flags for creation */
  CREATE_FLAG_DEBUG      =0x1,
};

typedef struct
{
  char caPathSetFanCtrlMode[260];
  char caPathEnableFan[260];
  char caPathSetPWM[260];
}TagCfg_AMDGPU;

typedef struct
{
  char caSensorReadPath[260];
}TagCfg_Sensor;

typedef struct
{
  int iTemp; /* In 1/10 °C */
  unsigned char ucFanSpeedPercent; /* 0..100% */
}TagCfg_Temperatures;

typedef struct TagFanCtrl_t TagFanCtrl;

TagFanCtrl *fanCtrl_Create(unsigned int uiUpdateDelayTime,
                           unsigned char ucFanSpeedHysteresis,
                           unsigned int *puiQuitRunFlag,
                           unsigned int uiFlags);

void fanCtrl_Destroy(TagFanCtrl *ptagFanCtrl);

int fanCtrl_AMDGPU_Init(TagFanCtrl *ptagFanCtrl,
                        const TagCfg_AMDGPU *pConfig,
                        const TagCfg_Sensor *ptagSensors,
                        unsigned int uiSensorsCount,
                        const TagCfg_Temperatures *ptagTemps,
                        unsigned int uiTempsCount);

int fanCtrl_AMDGPU_Reset(TagFanCtrl *ptagFanCtrl);

int fanCtrl_Run(TagFanCtrl *ptagFanCtrl);

#endif /* FANCTRL_H_INCLUDED */

