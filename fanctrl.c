#define POSIX_C_SOURCE 199309L /* For nanosleep function */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#define __STDC_FORMAT_MACROS
#include <inttypes.h> /* For PRIu64 */
#include <time.h>

#include "fanctrl.h"

#define STRINGIFY(x) STRINGIFY_DETAIL(x)
#define STRINGIFY_DETAIL(x) #x
#define DBG_PFX "fanctrl: @line:" STRINGIFY(__LINE__) ": "
#define DBG_PRINTF(str,...)  if(ptagFanCtrl->uiFlags&CREATE_FLAG_DEBUG) fprintf(stdout,DBG_PFX str "\n",__VA_ARGS__)
#define DBG_PUTS(str)        if(ptagFanCtrl->uiFlags&CREATE_FLAG_DEBUG) fputs(DBG_PFX str "\n",stdout)
#define ERR_PFX "fanctrl Error: @line:" STRINGIFY(__LINE__) ": "
#define ERR_PRINTF(str,...)  fprintf(stderr,ERR_PFX str "\n",__VA_ARGS__)
#define ERR_PUTS(str)        fputs(ERR_PFX str "\n",stderr)


#define MAX_TEMP_POINTS 32

typedef enum
{
  eFanCtrlType_AMDGPU=1,
}EFanCtrlType;

#define AMDGPU_SET_CTRL_MODE_AUTO       "2\n"
#define AMDGPU_SET_CTRL_MODE_MANUAL     "1\n"

#define AMDGPU_FAN_ENABLE               "1\n"
#define AMDGPU_FAN_DISABLE              "0\n"

enum
{
  AMDGPU_RAW_TO_TENTH_CELSUIS_DIVISOR   =100,

  CFG_LIMIT_MAX_DELAY_TIME              =300,   /* 30 seconds */
  CFG_LIMIT_MAX_TEMP                    =1500,  /* 150°C */
  CFG_LIMIT_MAX_HYSTERESIS              =30,    /* 30% */

  SENSOR_READ_MAX_RETRIES               =3,

  SENSOR_READ_RET_OK                    =0,
  SENSOR_READ_RET_TRYAGAIN              =1,
  SENSOR_READ_RET_FAILURE               =2,
};

#define AMDGPU_PWM_VAL_MIN 0
#define AMDGPU_PWM_VAL_MAX 255

#define AMDGPU_FANSPEED_PERCENT_TO_PWM ((float)(AMDGPU_PWM_VAL_MAX-AMDGPU_PWM_VAL_MIN)/100.0)

typedef struct
{
  int iTemp;                  /* In 1/10 °C */
  unsigned int uiFanSpeedPWM; /* PWM value */
}TagFanCtrlTempPoint;

typedef struct
{
  const char *pcSensorReadPath;
  long lRawValue;
  int iTempCelsius;
}TagFanCtrlSensor;

typedef struct
{
  const char *pcPathSetFanCtrlMode;
  const char *pcPathEnableFan;
  const char *pcPathSetPWM;
  int iLastUpdateTemp;
  unsigned int uiPointsCount;
  unsigned int uiSensorsCount;
  TagFanCtrlTempPoint *ptagPoints;
  TagFanCtrlSensor *ptagSensors;
}TagFanConfigAMDGPU;

struct TagFanCtrl_t
{
  unsigned int uiUpdateDelayTime;
  unsigned char ucTempHysteresisPercent;
  volatile unsigned int *puiQuitRunFlag;
  unsigned int uiFlags;
  TagFanConfigAMDGPU *ptagAMDGPU;
};

static int iFanCtrl_UpdateSensor_m(EFanCtrlType eSensorType,
                                   TagFanCtrlSensor *ptagSensor);

static int iFanCtrl_EnableFan(EFanCtrlType eSensorType,
                              const char *pcEnableFanPath,
                              int iEnable);

static int iFanCtrl_SetFanSpeed(EFanCtrlType eSensorType,
                                const char *pcSetFanSpeedPath,
                                unsigned int uiPWM);

static int amdgpu_SetMode(TagFanCtrl *ptagFanCtrl,
                          int iModeManual);

TagFanCtrl *fanCtrl_Create(unsigned int uiUpdateDelayTime,
                           unsigned char ucTempHysteresisPercent,
                           unsigned int *puiQuitRunFlag,
                           unsigned int uiFlags)
{
  TagFanCtrl *ptagFanCtrl;

  if(uiUpdateDelayTime > CFG_LIMIT_MAX_DELAY_TIME)
  {
    ERR_PRINTF("Invalid value: uiUpdateDelayTime too high(=%u), max=%u",
               uiUpdateDelayTime,
               CFG_LIMIT_MAX_DELAY_TIME);
    return(NULL);
  }
  if(ucTempHysteresisPercent > CFG_LIMIT_MAX_HYSTERESIS)
  {
    ERR_PRINTF("Invalid value: ucTempHysteresisPercent too high(=%u%%), max=%u%%",
               ucTempHysteresisPercent,
               CFG_LIMIT_MAX_HYSTERESIS);
    return(NULL);
  }

  if(!(ptagFanCtrl=malloc(sizeof(TagFanCtrl))))
  {
    ERR_PUTS("malloc() failed");
    return(NULL);
  }
  ptagFanCtrl->uiUpdateDelayTime=uiUpdateDelayTime;
  ptagFanCtrl->ucTempHysteresisPercent=ucTempHysteresisPercent;
  ptagFanCtrl->puiQuitRunFlag=puiQuitRunFlag;
  ptagFanCtrl->uiFlags=0;

  if(uiFlags & CREATE_FLAG_DEBUG)
    ptagFanCtrl->uiFlags|=CREATE_FLAG_DEBUG;

  ptagFanCtrl->ptagAMDGPU=NULL;

  DBG_PRINTF("Created New FanCtrl-Object\n"
             "->uiUpdateDelayTime=%u\n"
             "->ucTempHysteresisPercent=%u%%\n"
             "->uiFlags=0x%X",
             ptagFanCtrl->uiUpdateDelayTime,
             ptagFanCtrl->ucTempHysteresisPercent,
             ptagFanCtrl->uiFlags);

  return(ptagFanCtrl);
}

void fanCtrl_Destroy(TagFanCtrl *ptagFanCtrl)
{
  free(ptagFanCtrl->ptagAMDGPU);
  free(ptagFanCtrl);
}

int fanCtrl_ResetDevices(TagFanCtrl *ptagFanCtrl)
{
  int iRc=0;

  DBG_PUTS("Resetting Devices to Automode...");
  if(ptagFanCtrl->ptagAMDGPU)
  {
    DBG_PUTS("AMDGPU: Reset to automode");
    iRc|=amdgpu_SetMode(ptagFanCtrl,0);
  }
  return(iRc);
}

int fanCtrl_AMDGPU_Init(TagFanCtrl *ptagFanCtrl,
                        const TagCfg_AMDGPU *pConfig,
                        const TagCfg_Sensor *ptagSensors,
                        unsigned int uiSensorsCount,
                        const TagCfg_Temperatures *ptagTemps,
                        unsigned int uiTempsCount)
{
  unsigned int uiIndex;

  /* Verify parameters */
  if((uiTempsCount < 2) ||
     (uiSensorsCount == 0))
  {
    ERR_PUTS("Invalid configuration, at least 2 Temperature Points required / 1 sensor required");
    return(1);
  }

  /* Verify temperatures are in ascending order */
  for(uiIndex=1;uiIndex < uiTempsCount;++uiIndex)
  {
    if((ptagTemps[uiIndex].iTemp            > ptagTemps[uiIndex-1].iTemp) &&
       (ptagTemps[uiIndex].ucFanSpeedPercent > ptagTemps[uiIndex-1].ucFanSpeedPercent) &&
       (ptagTemps[uiIndex].iTemp < CFG_LIMIT_MAX_TEMP+1) &&
       (ptagTemps[uiIndex].ucFanSpeedPercent < 101))
      continue;

    ERR_PRINTF("Temperatures must be in ascending order, max. Temp=%d, max. Fanspeed=100, is: Temp=%d, Fanspeed=%u",
               CFG_LIMIT_MAX_TEMP,
               ptagTemps[uiIndex].iTemp,
               ptagTemps[uiIndex].ucFanSpeedPercent);
    return(2);
  }

  if(!(ptagFanCtrl->ptagAMDGPU=
       malloc(sizeof(TagFanConfigAMDGPU)+ sizeof(TagFanCtrlSensor)*uiSensorsCount + sizeof(TagFanCtrlTempPoint)*uiTempsCount)
     ))
  {
    ERR_PUTS("malloc() failed");
    return(3);
  }
  ptagFanCtrl->ptagAMDGPU->uiSensorsCount=uiSensorsCount;
  ptagFanCtrl->ptagAMDGPU->uiPointsCount=uiTempsCount;
  ptagFanCtrl->ptagAMDGPU->iLastUpdateTemp=0;
  ptagFanCtrl->ptagAMDGPU->pcPathSetFanCtrlMode=pConfig->caPathSetFanCtrlMode;
  ptagFanCtrl->ptagAMDGPU->pcPathEnableFan=pConfig->caPathEnableFan;
  ptagFanCtrl->ptagAMDGPU->pcPathSetPWM=pConfig->caPathSetPWM;

  ptagFanCtrl->ptagAMDGPU->ptagSensors=(TagFanCtrlSensor*)(((unsigned char*)ptagFanCtrl->ptagAMDGPU) + sizeof(TagFanConfigAMDGPU));
  ptagFanCtrl->ptagAMDGPU->ptagPoints=(TagFanCtrlTempPoint*)(((unsigned char*)ptagFanCtrl->ptagAMDGPU) + sizeof(TagFanConfigAMDGPU) + sizeof(TagFanCtrlSensor)*uiSensorsCount);

  DBG_PRINTF("Allocated space for AMDGPU: @0x%p\n"
             "Path: set_mode=\"%s\"\n"
             "Path: enable_fan=\"%s\"\n"
             "Path: set_pwm=\"%s\"\n"
             "->ptagSensors(%u)=@0x%p\n"
             "->ptagPoints(%u)=@0x%p",
             ptagFanCtrl->ptagAMDGPU,
             ptagFanCtrl->ptagAMDGPU->pcPathSetFanCtrlMode,
             ptagFanCtrl->ptagAMDGPU->pcPathEnableFan,
             ptagFanCtrl->ptagAMDGPU->pcPathSetPWM,
             uiSensorsCount,
             ptagFanCtrl->ptagAMDGPU->ptagSensors,
             uiTempsCount,
             ptagFanCtrl->ptagAMDGPU->ptagPoints);

  for(uiIndex=0; uiIndex < uiSensorsCount;++uiIndex) /* Initialize Sensors */
  {
    DBG_PRINTF("AMDGPU: Sensor[%u]=\"%s\"",
               uiIndex,
               ptagSensors[uiIndex].caSensorReadPath);
    ptagFanCtrl->ptagAMDGPU->ptagSensors[uiIndex].pcSensorReadPath=ptagSensors[uiIndex].caSensorReadPath;
  }

  for(uiIndex=0; uiIndex < uiTempsCount;++uiIndex) /* Initialize Temperature points */
  {
    DBG_PRINTF("AMDGPU: Temperature Point[%u]: Temp=%d, fanspeed=%u%%, %u PWM(calculated) ",
               uiIndex,
               ptagTemps[uiIndex].iTemp,
               ptagTemps[uiIndex].ucFanSpeedPercent,
               (unsigned int)(AMDGPU_FANSPEED_PERCENT_TO_PWM*ptagTemps[uiIndex].ucFanSpeedPercent+0.5));
    ptagFanCtrl->ptagAMDGPU->ptagPoints[uiIndex].iTemp=ptagTemps[uiIndex].iTemp;
    ptagFanCtrl->ptagAMDGPU->ptagPoints[uiIndex].uiFanSpeedPWM=(unsigned int)(AMDGPU_FANSPEED_PERCENT_TO_PWM*ptagTemps[uiIndex].ucFanSpeedPercent+0.5);
  }
  if(ptagFanCtrl->ptagAMDGPU->ptagPoints[0].uiFanSpeedPWM == 0) /* Uses Zero-Fan mode, needs adjustment to work properly */
  {
    DBG_PRINTF("Using Zero-Fan mode for low Temperature, adjusting point[0].temp to point[1].temp(%d)-1",
               ptagTemps[1].iTemp);
    ptagFanCtrl->ptagAMDGPU->ptagPoints[0].iTemp=ptagTemps[1].iTemp-1;
  }

  return(0);
}

int fanCtrl_Run(TagFanCtrl *ptagFanCtrl)
{
  struct timespec tagWaitTime;
  int iHighestSensorTempVal;
  unsigned int uiIndex;
  unsigned int uiCurrPWM;
  int iCurrFanState;
  int iSensorReadRetryCount;
  float fTmp;

  if(ptagFanCtrl->ptagAMDGPU)
  {
    if(amdgpu_SetMode(ptagFanCtrl,1))
    {
      DBG_PUTS("amdgpu_SetMode() failed");
      return(RUN_RET_ERR_INIT);
    }
    /* Initial enable fan */
    if(iFanCtrl_EnableFan(eFanCtrlType_AMDGPU,
                          ptagFanCtrl->ptagAMDGPU->pcPathEnableFan,
                          1))
    {
      DBG_PUTS("iFanCtrl_EnableFan() failed");
      return(RUN_RET_ERR_FAN_ENABLE);
    }
    iCurrFanState=1;
  }

  tagWaitTime.tv_nsec=(ptagFanCtrl->uiUpdateDelayTime%10)?(ptagFanCtrl->uiUpdateDelayTime%10) * 100000000:0;
  tagWaitTime.tv_sec=ptagFanCtrl->uiUpdateDelayTime/10;
  DBG_PRINTF("Wait time: %ld secs, %" PRIu64 "nsecs",
             tagWaitTime.tv_sec,
             tagWaitTime.tv_nsec);

  iSensorReadRetryCount=0;
  while((*ptagFanCtrl->puiQuitRunFlag) == 0)
  {
    nanosleep(&tagWaitTime,NULL);
    DBG_PRINTF("Timestamp=%" PRIu64 ", update temperatures...",time(NULL));
    /* Check if AMDGPU is used */
    if(ptagFanCtrl->ptagAMDGPU)
    {
      iHighestSensorTempVal=INT_MIN;
      /* Read AMDGPU Sensors */
      for(uiIndex=0;uiIndex < ptagFanCtrl->ptagAMDGPU->uiSensorsCount;++uiIndex)
      {
        switch(iFanCtrl_UpdateSensor_m(eFanCtrlType_AMDGPU,
                                       &ptagFanCtrl->ptagAMDGPU->ptagSensors[uiIndex]))
        {
          case SENSOR_READ_RET_OK: /* OK */
            iSensorReadRetryCount=0;
            break;
          case SENSOR_READ_RET_TRYAGAIN: /* Temporary failure, try again if < max tries */
            if(iSensorReadRetryCount++ < SENSOR_READ_MAX_RETRIES)
            {
              ERR_PRINTF("Temporary failure reading sensor, retry(%d/%d)...",
                         iSensorReadRetryCount,
                         SENSOR_READ_MAX_RETRIES);
              break;
            }
            /* Fall-through */
          case SENSOR_READ_RET_FAILURE: /* quit with error */
          default:
            ERR_PUTS("iGpuFanCtrl_UpdateSensor() failed");
            return(RUN_RET_ERR_SENSOR_READ);
        }
        DBG_PRINTF("Current Sensor[%u]\n"
                   "\"%s\": Rawvalue=%ld, 1/10°C=%u",
                   uiIndex,
                   ptagFanCtrl->ptagAMDGPU->ptagSensors[uiIndex].pcSensorReadPath,
                   ptagFanCtrl->ptagAMDGPU->ptagSensors[uiIndex].lRawValue,
                   ptagFanCtrl->ptagAMDGPU->ptagSensors[uiIndex].iTempCelsius);

        if(ptagFanCtrl->ptagAMDGPU->ptagSensors[uiIndex].iTempCelsius > iHighestSensorTempVal)
          iHighestSensorTempVal=ptagFanCtrl->ptagAMDGPU->ptagSensors[uiIndex].iTempCelsius;
      }
      if(ptagFanCtrl->ptagAMDGPU->iLastUpdateTemp == iHighestSensorTempVal)
      {/* No temperature change, continue */
        DBG_PUTS("No Temperature change");
        continue;
      }
      uiIndex=(iHighestSensorTempVal > ptagFanCtrl->ptagAMDGPU->iLastUpdateTemp)?iHighestSensorTempVal-ptagFanCtrl->ptagAMDGPU->iLastUpdateTemp:ptagFanCtrl->ptagAMDGPU->iLastUpdateTemp-iHighestSensorTempVal;

      DBG_PRINTF("iLastUpdateTemp=%d Temperature change=%u, HysteresisTemp=%u, ",
                 ptagFanCtrl->ptagAMDGPU->iLastUpdateTemp,
                 uiIndex,
                 (unsigned int)((float)ptagFanCtrl->ucTempHysteresisPercent*ptagFanCtrl->ptagAMDGPU->iLastUpdateTemp/100.0+0.5));
      if(uiIndex < (unsigned int)((float)ptagFanCtrl->ucTempHysteresisPercent*ptagFanCtrl->ptagAMDGPU->iLastUpdateTemp/100.0+0.5))
      {/* No Fanspeed update needed */
        DBG_PUTS("No Fanspeed update required, temperature hysteresis below configured value");
        continue;
      }
      ptagFanCtrl->ptagAMDGPU->iLastUpdateTemp=iHighestSensorTempVal;

      /* Update AMDGPU Fanspeed if needed */
      for(uiIndex=0;uiIndex < ptagFanCtrl->ptagAMDGPU->uiPointsCount;++uiIndex)
      {/* Find closest defined temperature point */
        if(iHighestSensorTempVal > ptagFanCtrl->ptagAMDGPU->ptagPoints[uiIndex].iTemp)
          continue;
        break;
      }

      if(ptagFanCtrl->ptagAMDGPU->ptagPoints[uiIndex].uiFanSpeedPWM)
      {
        if((uiIndex == 0) ||
           (uiIndex == ptagFanCtrl->ptagAMDGPU->uiPointsCount) ||
           (ptagFanCtrl->ptagAMDGPU->ptagPoints[uiIndex].iTemp==iHighestSensorTempVal))
        {/* Is bigger than highest or lower than lowest temperature point or exactly on one point */
          if(uiIndex == ptagFanCtrl->ptagAMDGPU->uiPointsCount)
            --uiIndex;

          uiCurrPWM=ptagFanCtrl->ptagAMDGPU->ptagPoints[uiIndex].uiFanSpeedPWM*100;
        }
        else /* Target is between 2 defined points */
        {
          /* Calculate delta between 2 defined points (P-lower and P-Higher), Dlp */
          uiCurrPWM=(ptagFanCtrl->ptagAMDGPU->ptagPoints[uiIndex].iTemp-ptagFanCtrl->ptagAMDGPU->ptagPoints[uiIndex-1].iTemp);
          /* Calculate delta between current temperature point and lower defined point. Then divide by Delta Dlp. */
          fTmp=(float)uiCurrPWM / (iHighestSensorTempVal-ptagFanCtrl->ptagAMDGPU->ptagPoints[uiIndex-1].iTemp);
          /* Calculate percentage of fan speed (multilied by factor 100) */
          fTmp=((float)(ptagFanCtrl->ptagAMDGPU->ptagPoints[uiIndex].uiFanSpeedPWM-ptagFanCtrl->ptagAMDGPU->ptagPoints[uiIndex-1].uiFanSpeedPWM))/fTmp;
          uiCurrPWM=(ptagFanCtrl->ptagAMDGPU->ptagPoints[uiIndex-1].uiFanSpeedPWM*100)+(unsigned int)(fTmp*100+0.5);
        }
      }
      else
        uiCurrPWM=0;

      DBG_PRINTF("Calculated fanspeed (c->pwm fac=%f) %u pwm (~%f percent) for temp. %d",
                 AMDGPU_FANSPEED_PERCENT_TO_PWM,
                 (uiCurrPWM+50)/100,
                 (uiCurrPWM+50)/100/AMDGPU_FANSPEED_PERCENT_TO_PWM,
                 iHighestSensorTempVal);
      uiCurrPWM=(uiCurrPWM+50)/100;

      if(((uiCurrPWM) && (iCurrFanState == 0)) ||     /* Fan needs to be enabled */
         ((uiCurrPWM == 0) && (iCurrFanState == 1)))  /* Fan needs to be disabled */
      {
        iCurrFanState=(uiCurrPWM)?1:0;
        DBG_PRINTF("Fanstate changed: %s",
                   (iCurrFanState)?"ENABLE":"DISABLE");

        if(iFanCtrl_EnableFan(eFanCtrlType_AMDGPU,
                              ptagFanCtrl->ptagAMDGPU->pcPathEnableFan,
                              iCurrFanState))
        {
          DBG_PUTS("iFanCtrl_EnableFan() failed");
          return(RUN_RET_ERR_FAN_ENABLE);
        }
      }

      if(uiCurrPWM)
      {
        if(iFanCtrl_SetFanSpeed(eFanCtrlType_AMDGPU,
                                ptagFanCtrl->ptagAMDGPU->pcPathSetPWM,
                                uiCurrPWM))
        {
          DBG_PUTS("iFanCtrl_SetFanSpeed() failed");
          return(RUN_RET_ERR_PWM_WRITE);
        }
      }
    }
  }

  DBG_PUTS("Stopping loop...");
  return(RUN_RET_OK);
}

static int iFanCtrl_UpdateSensor_m(EFanCtrlType eSensorType,
                                   TagFanCtrlSensor *ptagSensor)
{
  FILE *fp;
  char caBuf[16];
  char *pcTmp;

  if(!(fp=fopen(ptagSensor->pcSensorReadPath,"r")))
  {
    ERR_PRINTF("fopen(\"%s\",\"w\") failed (%d): %s",
               ptagSensor->pcSensorReadPath,
               errno,
               strerror(errno));
    return(SENSOR_READ_RET_FAILURE);
  }
  if(fgets(caBuf,sizeof(caBuf),fp) == NULL)
  {
    ERR_PRINTF("fgets(caBuf,%" PRIu64 ",fp) failed (%d): %s",
               sizeof(caBuf),
               errno,
               strerror(errno));
    fclose(fp);
    return((errno == EIO) ? SENSOR_READ_RET_TRYAGAIN : SENSOR_READ_RET_FAILURE);
  }
  fclose(fp);

  switch(eSensorType)
  {
    case eFanCtrlType_AMDGPU:
      ptagSensor->lRawValue=strtol(caBuf,&pcTmp,10);
      if((ptagSensor->lRawValue == 0) ||
         ((ptagSensor->lRawValue == LONG_MAX) && (errno==ERANGE)) ||
         ((ptagSensor->lRawValue == LONG_MIN) && (errno==ERANGE)) ||
         (*pcTmp != '\n'))
      {/* Conversion failed*/
        ERR_PUTS("Conversion string-> long failed");
        return(SENSOR_READ_RET_FAILURE);
      }
      /* Calculate Temperature in Celsius */
      ptagSensor->iTempCelsius=(int)(ptagSensor->lRawValue/AMDGPU_RAW_TO_TENTH_CELSUIS_DIVISOR);
      break;
    default:
      ERR_PRINTF("Unknown Sensortype (%d)",eSensorType);
      return(SENSOR_READ_RET_FAILURE);
  }
  return(SENSOR_READ_RET_OK);
}

static int iFanCtrl_EnableFan(EFanCtrlType eSensorType,
                              const char *pcEnableFanPath,
                              int iEnable)
{
  FILE *fp;
  int iRc=0;

  if(!(fp=fopen(pcEnableFanPath,"w")))
  {
    ERR_PRINTF("fopen(\"%s\",\"w\") failed (%d): %s",
               pcEnableFanPath,
               errno,
               strerror(errno));
    if(errno == EACCES)
      ERR_PUTS("Application should run as root.");
    return(1);
  }
  switch(eSensorType)
  {
    case eFanCtrlType_AMDGPU:
      if(fputs((iEnable)?AMDGPU_FAN_ENABLE:AMDGPU_FAN_DISABLE,
               fp) == EOF)
      {
        ERR_PRINTF("fputs(\"%s\",fp) failed (%d): %s",
                   (iEnable)?AMDGPU_FAN_ENABLE:AMDGPU_FAN_DISABLE,
                   errno,
                   strerror(errno));
        iRc=1;
      }
      break;
    default:
      ERR_PRINTF("Unknown Sensortype (%d)",eSensorType);
      iRc=2;
      break;
  }
  fclose(fp);
  return(iRc);
}

static int iFanCtrl_SetFanSpeed(EFanCtrlType eSensorType,
                                const char *pcSetFanSpeedPath,
                                unsigned int uiPWM)
{
  FILE *fp;
  int iRc=0;

  if(!(fp=fopen(pcSetFanSpeedPath,"w")))
  {
    ERR_PRINTF("fopen(\"%s\",\"w\") failed (%d): %s",
               pcSetFanSpeedPath,
               errno,
               strerror(errno));
    if(errno == EACCES)
      ERR_PUTS("Application should run as root.");
    return(1);
  }
  switch(eSensorType)
  {
    case eFanCtrlType_AMDGPU:
      if(fprintf(fp,
                 "%u\n",
                 uiPWM) < 1)
      {
        ERR_PRINTF("fprintf(fp,\"%%u\",%u) failed (%d): %s",
                   uiPWM,
                   errno,
                   strerror(errno));
        iRc=1;
      }
      break;
    default:
      ERR_PRINTF("Unknown Sensortype (%d)",eSensorType);
      iRc=2;
      break;
  }
  fclose(fp);
  return(iRc);
}

/**
 * AMDGPU Functions
 */
static int amdgpu_SetMode(TagFanCtrl *ptagFanCtrl,
                          int iModeManual)
{
  FILE * fp;
  int iRc=0;

  if(!(fp=fopen(ptagFanCtrl->ptagAMDGPU->pcPathSetFanCtrlMode,"w")))
  {
    ERR_PRINTF("fopen(\"%s\",\"w\") failed (%d): %s",
               ptagFanCtrl->ptagAMDGPU->pcPathSetFanCtrlMode,
               errno,
               strerror(errno));
    if(errno == EACCES)
      ERR_PUTS("Application should run as root.");
    return(1);
  }
  if(fputs((iModeManual)?AMDGPU_SET_CTRL_MODE_MANUAL:AMDGPU_SET_CTRL_MODE_AUTO,
           fp) == EOF)
  {
    ERR_PRINTF("fputs(%s,fp) failed (%d): %s",
               (iModeManual)?AMDGPU_SET_CTRL_MODE_MANUAL:AMDGPU_SET_CTRL_MODE_AUTO,
               errno,
               strerror(errno));
    iRc=2;
  }
  fclose(fp);
  return(iRc);
}

