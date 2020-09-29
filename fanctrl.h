#ifndef FANCTRL_H_INCLUDED
  #define FANCTRL_H_INCLUDED

#define FANCTRL_VERSION_MAJOR  0
#define FANCTRL_VERSION_MINOR  2
#define FANCTRL_VERSION_PATCH  0
#define FANCTRL_VERSION_STATUS "beta"

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

/**
 * Configuration for AMDGPU.
 * Make sure that all memory stays valid during runtime, paths are only stored as reference!
 */
typedef struct
{
  char caPathSetFanCtrlMode[260];
  char caPathEnableFan[260];
  char caPathSetPWM[260];
}TagCfg_AMDGPU;

/**
 * Configuration for Sensors.
 * Make sure that all memory stays valid during runtime, paths are only stored as reference!
 */
typedef struct
{
  char caSensorReadPath[260];
}TagCfg_Sensor;

/**
 * Configuration for Temperature Points.
 */
typedef struct
{
  /**
   * Temperature, in 1/10 °C
   */
  int iTemp;
  /**
   * Desired Fanspeed at temperature, in Percent.
   */
  unsigned char ucFanSpeedPercent;
}TagCfg_Temperatures;

typedef struct TagFanCtrl_t TagFanCtrl;


/**
 * Creates new Object for Fancontrol.
 * Specific devices should be attached afterwards.
 *
 * @param uiUpdateDelayTime
 *                _IN_ Delay Time between updating the Sensorvalues/Fanspeeds, in 1/10 seconds.
 * @param ucTempHysteresisPercent
 *                _IN_ Temperature Hysteresis, before the speed is updated, in Percent.
 * @param puiQuitRunFlag
 *                _IN_ Flag to indicate the Run Function to quit.
 *                Value should be 0 before Run is called, otherwise it will instantly quit.
 * @param uiFlags _IN_ Further configuration flags, see CREATE_FLAG_ enums above for options.
 *
 * @return New Fanctrl Object on success, NULL on error.
 */
TagFanCtrl *fanCtrl_Create(unsigned int uiUpdateDelayTime,
                           unsigned char ucTempHysteresisPercent,
                           unsigned int *puiQuitRunFlag,
                           unsigned int uiFlags);

/**
 * Cleans up the allocated memory of the Fanctrl-Object.
 * After this, the FanCtrl-Object cannot be used anymore.
 * Make sure to reset Fancontrol to automode, by calling fanCtrl_ResetDevices() before this.
 *
 * @param ptagFanCtrl
 *               _IN_ Fanctrl-Object to cleanup
 */
void fanCtrl_Destroy(TagFanCtrl *ptagFanCtrl);

/**
 * Initializes the AMDGPU Subsystem for Fancontol of a GPU using the AMDGPU-Driver.
 *
 * @param ptagFanCtrl
 *                  _IN_ The FanCtrl-Object
 * @param pConfig   _IN_ Configuration for AMDGPU.
 * @param ptagSensors
 *                  _IN_ Sensor configuration
 * @param uiSensorsCount
 *                  _IN_ Number of Sensors
 * @param ptagTemps _IN_ Temperature Points, containing Temperature + according fanspeed.
 * @param uiTempsCount
 *                  _IN_ Number of Temperature Points
 *
 * @return 0 on success, nonzero on error.
 */
int fanCtrl_AMDGPU_Init(TagFanCtrl *ptagFanCtrl,
                        const TagCfg_AMDGPU *pConfig,
                        const TagCfg_Sensor *ptagSensors,
                        unsigned int uiSensorsCount,
                        const TagCfg_Temperatures *ptagTemps,
                        unsigned int uiTempsCount);


/**
 * Resets the configured devices to automatic Fanctrl.
 *
 * @param ptagFanCtrl
 *               _IN_ The FanCtrl-Object
 *
 * @return 0 on success, nonzero on failure.
 */
int fanCtrl_ResetDevices(TagFanCtrl *ptagFanCtrl);

/**
 * Starts the Fancontrol. This will block the current thread, until the exit Flag is set to quit.
 *
 * @param ptagFanCtrl
 *               _IN_ The FanCtrl-Object
 *
 * @return RUN_RET_OK If execution was successful, Errorcode on failure.
 */
int fanCtrl_Run(TagFanCtrl *ptagFanCtrl);

#endif /* FANCTRL_H_INCLUDED */

