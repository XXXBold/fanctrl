#include <stdlib.h>
#include <stdio.h>
#include <signal.h>

#include "fanctrl.h"
#include "inifile.h"

#define CFGFILE_SECTION_NAME_FANCTRL                 "FanCtrlGlobal"
#define CFGFILE_SECTION_NAME_AMDGPU                  "AMDGPU"

#define CFGFILE_KEY_NAME_FANCTRL_UPDATETIME          "UpdateDelayTime"
#define CFGFILE_KEY_NAME_FANCTRL_CHANGE_HYSTERESIS   "TempChangeHysteresis"

#define CFGFILE_KEY_NAME_AMDGPU_PATH_SET_CTRL_MODE   "PathSetFanCtrlMode"
#define CFGFILE_KEY_NAME_AMDGPU_PATH_ENABLE_FAN      "PathEnableFan"
#define CFGFILE_KEY_NAME_AMDGPU_PATH_SET_PWM         "PathSetPWM"

#define STRINGIFY(x) STRINGIFY_DETAIL(x)
#define STRINGIFY_DETAIL(x) #x
#define ERR_PFX                                      "fanctrl_cli Error: @line:" STRINGIFY(__LINE__) ": "
#define ERR_PRINTF(str,...)                          fprintf(stderr,ERR_PFX str "\n",__VA_ARGS__)
#define ERR_PUTS(str)                                fputs(ERR_PFX str "\n",stderr)

enum
{
  MAX_SENSORS_COUNT=10,
  MAX_TEMPERATURES_COUNT=32,

  CLI_OPTION_FLAG_PRINT_HELP=0x1,
  CLI_OPTION_FLAG_PRINT_VERSION=0x2,
  CLI_OPTION_FLAG_DEBUG=0x4,

  CLI_CMD_INDEX_HELP=0,
  CLI_CMD_INDEX_VERSION,
  CLI_CMD_INDEX_DEBUG
};

typedef struct
{
  const char *pcCmd;
  const char *pcAlias;
}TagCLICommands;

static const TagCLICommands tagaCLICommands_m[]={
  {"--help",    "-h"},
  {"--version", "-v"},
  {"--debug",   "-d"},
};

int iCleanupFanControl(int iRc);
void vSignalHandler(int iSignum);

static void vCLIPrintHelp(void);
static void vCLIPrintWrongArgs(const char *pcArg);

static int iParseCLI_m(int argc,
                       char *argv[],
                       unsigned int *puiOptions);

static int iFanCtrl_ReadCfgFile(const char *pcFilePath,
                                unsigned int *puiUpdateDelayTime,
                                unsigned char *pucChangeHysteresis,
                                TagCfg_AMDGPU *ptagAMDGPU,
                                TagCfg_Sensor tagaSensors[MAX_SENSORS_COUNT],
                                TagCfg_Temperatures tagaTemps[MAX_TEMPERATURES_COUNT],
                                unsigned int *puiSensorsCount,
                                unsigned int *puiTempsCount);

static const char *pcaCFGKeys_AMDGPU_Sensors_m[]={"PathSensorRead1",
                                                  "PathSensorRead2",
                                                  "PathSensorRead3",
                                                  "PathSensorRead4",
                                                  "PathSensorRead5",
                                                  "PathSensorRead6",
                                                  "PathSensorRead7",
                                                  "PathSensorRead8",
                                                  "PathSensorRead9",
                                                  "PathSensorRead10"};

static const char *pcaCFGKeys_AMDGPU_Temps_m[]={"FanSpeed1",
                                                "FanSpeed2",
                                                "FanSpeed3",
                                                "FanSpeed4",
                                                "FanSpeed5",
                                                "FanSpeed6",
                                                "FanSpeed7",
                                                "FanSpeed8",
                                                "FanSpeed9",
                                                "FanSpeed10",
                                                "FanSpeed11",
                                                "FanSpeed12",
                                                "FanSpeed13",
                                                "FanSpeed14",
                                                "FanSpeed15",
                                                "FanSpeed16",
                                                "FanSpeed17",
                                                "FanSpeed18",
                                                "FanSpeed19",
                                                "FanSpeed20",
                                                "FanSpeed21",
                                                "FanSpeed22",
                                                "FanSpeed23",
                                                "FanSpeed24",
                                                "FanSpeed25",
                                                "FanSpeed26",
                                                "FanSpeed27",
                                                "FanSpeed28",
                                                "FanSpeed29",
                                                "FanSpeed30",
                                                "FanSpeed31",
                                                "FanSpeed32"};

unsigned int uiExitFanCtrlFlag_m;
static TagFanCtrl *ptagFanCtrl_m;

int main(int argc, char *argv[])
{

  TagCfg_AMDGPU tagConfig;
  TagCfg_Sensor tagaSensors[MAX_SENSORS_COUNT];
  TagCfg_Temperatures tagaTemps[MAX_TEMPERATURES_COUNT];
  unsigned int uiSensorsCount;
  unsigned int uiTemperaturesCount;
  unsigned int uiUpdateTime;
  unsigned char ucChangeHysteresis;
  unsigned int uiCLIOptions;
  unsigned int uiCreateFlags=0;

  if(iParseCLI_m(argc,
                 argv,
                 &uiCLIOptions))
  {
    vCLIPrintWrongArgs((argc > 1)?argv[1]:NULL);
    return(EXIT_FAILURE);
  }
  if(uiCLIOptions & CLI_OPTION_FLAG_PRINT_HELP)
  {
    vCLIPrintHelp();
    return(EXIT_SUCCESS);
  }
  if(uiCLIOptions & CLI_OPTION_FLAG_PRINT_VERSION)
  {
    printf("fanctrl version: %u.%u.%u (%s)\n",
           FANCTRL_VERSION_MAJOR,
           FANCTRL_VERSION_MINOR,
           FANCTRL_VERSION_PATCH,
           FANCTRL_VERSION_STATUS);
    return(EXIT_SUCCESS);
  }
  if(uiCLIOptions & CLI_OPTION_FLAG_DEBUG)
    uiCreateFlags|=CREATE_FLAG_DEBUG;

  if(iFanCtrl_ReadCfgFile(argv[1],
                          &uiUpdateTime,
                          &ucChangeHysteresis,
                          &tagConfig,
                          tagaSensors,
                          tagaTemps,
                          &uiSensorsCount,
                          &uiTemperaturesCount))
  {
    ERR_PUTS("iFanCtrl_ReadCfgFile() failed");
    return(EXIT_FAILURE);
  }

  uiExitFanCtrlFlag_m=0;

  signal(SIGINT,vSignalHandler);   /* On CTRL+C */
  signal(SIGTERM,vSignalHandler);  /* On exit using kill (SIGTERM) command */

  if(!(ptagFanCtrl_m=fanCtrl_Create(uiUpdateTime,
                                    ucChangeHysteresis,
                                    &uiExitFanCtrlFlag_m,
                                    uiCreateFlags)))
  {
    ERR_PUTS("InternalError: fanCtrl_Create() failed");
    return(EXIT_FAILURE);
  }

  if(fanCtrl_AMDGPU_Init(ptagFanCtrl_m,
                         &tagConfig,
                         tagaSensors,
                         uiSensorsCount,
                         tagaTemps,
                         uiTemperaturesCount))
  {
    ERR_PUTS("fanCtrl_AMDGPU_Init() failed");
    fanCtrl_Destroy(ptagFanCtrl_m);
    return(EXIT_FAILURE);
  }

  return(iCleanupFanControl(fanCtrl_Run(ptagFanCtrl_m)));
}

void vSignalHandler(int iSignum)
{
  switch(iSignum)
  {
    case SIGINT:
    case SIGTERM:
      uiExitFanCtrlFlag_m=1; /* Quits run function loop */
      break;
    default:
      ERR_PRINTF("Unknown signal: %d",iSignum);
      break;
  }
}

int iCleanupFanControl(int iRc)
{
  switch(iRc)
  {
    case RUN_RET_ERR_INIT:
      break;
    case RUN_RET_OK:
    default:
      if(fanCtrl_ResetDevices(ptagFanCtrl_m))
      {
        iRc=1;
        ERR_PUTS("fanCtrl_AMDGPU_Reset() failed! Fanspeed stays in manual mode!");
      }
  }
  fanCtrl_Destroy(ptagFanCtrl_m);
  return((iRc)?EXIT_FAILURE:EXIT_SUCCESS);
}

static void vCLIPrintHelp(void)
{
  printf("Usage: fanctrl [options]\n"
         "Available Options:\n"
         "  <Path to config file>: Path to configuration file\n"
         "    %s, %s: Print debug informations during runtime, this is recommended while testing your configuration\n"
         "  %s, %s: Prints this help\n"
         "  %s, %s: Prints version of the application\n",
         tagaCLICommands_m[CLI_CMD_INDEX_DEBUG].pcCmd,
         tagaCLICommands_m[CLI_CMD_INDEX_DEBUG].pcAlias,
         tagaCLICommands_m[CLI_CMD_INDEX_HELP].pcCmd,
         tagaCLICommands_m[CLI_CMD_INDEX_HELP].pcAlias,
         tagaCLICommands_m[CLI_CMD_INDEX_VERSION].pcCmd,
         tagaCLICommands_m[CLI_CMD_INDEX_VERSION].pcAlias);
}

static void vCLIPrintWrongArgs(const char *pcArg)
{
  if(pcArg)
    printf("fanctrl: Unknown option: \'%s\'\n"
           "Use \'fanctrl %s\' for more information\n",
           pcArg,
           tagaCLICommands_m[CLI_CMD_INDEX_HELP].pcCmd);
  else
    printf("fanctrl: No parameters specified\n"
           "Use \'fanctrl %s\' for more information\n",
           tagaCLICommands_m[CLI_CMD_INDEX_HELP].pcCmd);

}

static int iParseCLI_m(int argc,
                       char *argv[],
                       unsigned int *puiOptions)
{
  *puiOptions=0;
  if(argc < 2)
    return(1);

  if((strcmp(argv[1],tagaCLICommands_m[CLI_CMD_INDEX_HELP].pcCmd)   == 0) ||
     (strcmp(argv[1],tagaCLICommands_m[CLI_CMD_INDEX_HELP].pcAlias) == 0))
  {
    *puiOptions|=CLI_OPTION_FLAG_PRINT_HELP;
    return(0); /* No more parsing needed */
  }
  if((strcmp(argv[1],tagaCLICommands_m[CLI_CMD_INDEX_VERSION].pcCmd)   == 0) ||
     (strcmp(argv[1],tagaCLICommands_m[CLI_CMD_INDEX_VERSION].pcAlias) == 0))
  {
    *puiOptions|=CLI_OPTION_FLAG_PRINT_VERSION;
    return(0); /* No more parsing needed */
  }

  if(*argv[1] != '/') /* First argument must be a path, if it's not help */
  {
    return(1);
  }
  if(argc == 2) /* No more args than config file path, OK */
    return(0);

  if(argc == 3) /* Check for debug switch */
  {
    if((strcmp(argv[2],tagaCLICommands_m[CLI_CMD_INDEX_DEBUG].pcCmd)   == 0) ||
       (strcmp(argv[2],tagaCLICommands_m[CLI_CMD_INDEX_DEBUG].pcAlias) == 0))
    {
      *puiOptions|=CLI_OPTION_FLAG_DEBUG;
      return(0); /* Okay, enable debug outputs */
    }
  }
  return(1);
}

static int iFanCtrl_ReadCfgFile(const char *pcFilePath,
                                unsigned int *puiUpdateDelayTime,
                                unsigned char *pucChangeHysteresis,
                                TagCfg_AMDGPU *ptagAMDGPU,
                                TagCfg_Sensor tagaSensors[MAX_SENSORS_COUNT],
                                TagCfg_Temperatures tagaTemps[MAX_TEMPERATURES_COUNT],
                                unsigned int *puiSensorsCount,
                                unsigned int *puiTempsCount)
{
  /* Local macros for error handling */
#define ERR_INI_FAILURE(txt) ERR_PRINTF( \
  txt, \
  pcCurrKey, \
  pcCurrSection, \
  iRc, \
  IniFile_GetErrorText(iRc)); \
  IniFile_Dispose(tagFile); \
  return(1);

#define ERR_INI_SECT_FIND()     ERR_INI_FAILURE("Key \"%s\" in Section \"%s\": Section not found: Failure (%d): %s")
#define ERR_INI_KEY_FIND()      ERR_INI_FAILURE("Key \"%s\" in Section \"%s\": Key not found: Failure (%d): %s")
#define ERR_INI_GET_KEY_VALUE() ERR_INI_FAILURE("Key \"%s\" in Section \"%s\": Failed getting Value from Key: Failure (%d): %s")

  Inifile tagFile;
  TagData tagCfgData;
  char caTmp[20];
  char *pcTmp;
  unsigned int uiIndex;
  const char *pcCurrSection;
  const char *pcCurrKey;
  long lTmp;
  int iRc;

  if((iRc=IniFile_New(&tagFile,
                      pcFilePath,
                      INI_OPT_CASE_SENSITIVE)) != INI_ERR_NONE)
  {
    ERR_PRINTF("IniFile_New(): Failed (%d): %s",
               iRc,
               IniFile_GetErrorText(iRc));
    return(1);
  }
  if((iRc=IniFile_Read(tagFile)) != INI_ERR_NONE)
  {
    ERR_PRINTF("Failed to read config file (%d): %s",
               iRc,
               IniFile_GetErrorText(iRc));

    IniFile_Dispose(tagFile);
    return(1);
  }

  pcCurrSection=CFGFILE_SECTION_NAME_FANCTRL;
  pcCurrKey=CFGFILE_KEY_NAME_FANCTRL_UPDATETIME;
  if((iRc=IniFile_Iterator_FindSection(tagFile,
                                       pcCurrSection)) != INI_ERR_NONE)
  {
    ERR_INI_SECT_FIND();
  }

  if((iRc=IniFile_Iterator_FindKey(tagFile,
                                   pcCurrKey)) != INI_ERR_NONE)
  {
    ERR_INI_KEY_FIND();
  }

  dataType_Set_Uint(&tagCfgData,0,eRepr_Int_Default);
  if((iRc=IniFile_Iterator_KeyGetValue(tagFile,
                                       &tagCfgData)) != INI_ERR_NONE)
  {
    ERR_INI_GET_KEY_VALUE();
  }
  *puiUpdateDelayTime=DATA_GET_UINT(tagCfgData);

  pcCurrKey=CFGFILE_KEY_NAME_FANCTRL_CHANGE_HYSTERESIS;
  if((iRc=IniFile_Iterator_FindKey(tagFile,
                                   pcCurrKey)) != INI_ERR_NONE)
  {
    ERR_INI_KEY_FIND();
  }

  dataType_Set_Uint(&tagCfgData,0,eRepr_Int_Default);
  if((iRc=IniFile_Iterator_KeyGetValue(tagFile,
                                       &tagCfgData)) != INI_ERR_NONE)
  {
    ERR_INI_GET_KEY_VALUE();
  }
  *pucChangeHysteresis=(unsigned char)DATA_GET_UINT(tagCfgData);

  pcCurrSection=CFGFILE_SECTION_NAME_AMDGPU;
  if((iRc=IniFile_Iterator_FindSection(tagFile,
                                       pcCurrSection)) != INI_ERR_NONE)
  {
    ERR_INI_SECT_FIND();
  }

  pcCurrKey=CFGFILE_KEY_NAME_AMDGPU_PATH_SET_CTRL_MODE;
  if((iRc=IniFile_Iterator_FindKey(tagFile,
                                   pcCurrKey)) != INI_ERR_NONE)
  {
    ERR_INI_KEY_FIND();
  }

  dataType_Set_String(&tagCfgData,
                      ptagAMDGPU->caPathSetFanCtrlMode,
                      sizeof(ptagAMDGPU->caPathSetFanCtrlMode),
                      NULL,
                      0,
                      eRepr_String_Default);
  if((iRc=IniFile_Iterator_KeyGetValue(tagFile,
                                       &tagCfgData)) != INI_ERR_NONE)
  {
    ERR_INI_GET_KEY_VALUE();
  }

  pcCurrKey=CFGFILE_KEY_NAME_AMDGPU_PATH_ENABLE_FAN;
  if((iRc=IniFile_Iterator_FindKey(tagFile,
                                   pcCurrKey)) != INI_ERR_NONE)
  {
    ERR_INI_KEY_FIND();
  }

  dataType_Set_String(&tagCfgData,
                      ptagAMDGPU->caPathEnableFan,
                      sizeof(ptagAMDGPU->caPathEnableFan),
                      NULL,
                      0,
                      eRepr_String_Default);
  if((iRc=IniFile_Iterator_KeyGetValue(tagFile,
                                       &tagCfgData)) != INI_ERR_NONE)
  {
    ERR_INI_GET_KEY_VALUE();
  }

  pcCurrKey=CFGFILE_KEY_NAME_AMDGPU_PATH_SET_PWM;
  if((iRc=IniFile_Iterator_FindKey(tagFile,
                                   pcCurrKey)) != INI_ERR_NONE)
  {
    ERR_INI_KEY_FIND();
  }

  dataType_Set_String(&tagCfgData,
                      ptagAMDGPU->caPathSetPWM,
                      sizeof(ptagAMDGPU->caPathSetPWM),
                      NULL,
                      0,
                      eRepr_String_Default);
  if((iRc=IniFile_Iterator_KeyGetValue(tagFile,
                                       &tagCfgData)) != INI_ERR_NONE)
  {
    ERR_INI_GET_KEY_VALUE();
  }

  /* Read All Sensor Paths */
  for(uiIndex=0;uiIndex < MAX_SENSORS_COUNT;++uiIndex)
  {
    pcCurrKey=pcaCFGKeys_AMDGPU_Sensors_m[uiIndex];
    if((iRc=IniFile_Iterator_FindKey(tagFile,
                                     pcCurrKey)) != INI_ERR_NONE)
    {
      if(iRc == INI_ERR_FIND_SECTION) /* No more Sensors */
        break;
      ERR_INI_KEY_FIND();
    }

    dataType_Set_String(&tagCfgData,
                        tagaSensors[uiIndex].caSensorReadPath,
                        sizeof(tagaSensors[uiIndex].caSensorReadPath),
                        NULL,
                        0,
                        eRepr_String_Default);
    if((iRc=IniFile_Iterator_KeyGetValue(tagFile,
                                         &tagCfgData)) != INI_ERR_NONE)
    {
      ERR_INI_GET_KEY_VALUE();
    }
  }
  if(uiIndex == 0) /* Check if sensors where found */
  {
    ERR_PUTS("No Sensor paths found in config file");

    IniFile_Dispose(tagFile);
    return(1);
  }
  *puiSensorsCount=uiIndex;

  dataType_Set_String(&tagCfgData,
                      caTmp,
                      sizeof(caTmp),
                      NULL,
                      0,
                      eRepr_String_Default);
  /* Read All Temperature Points */
  for(uiIndex=0;uiIndex < MAX_TEMPERATURES_COUNT;++uiIndex)
  {
    pcCurrKey=pcaCFGKeys_AMDGPU_Temps_m[uiIndex];
    if((iRc=IniFile_Iterator_FindKey(tagFile,
                                     pcCurrKey)) != INI_ERR_NONE)
    {
      if(iRc == INI_ERR_FIND_SECTION) /* No more Temperatures */
        break;
      ERR_INI_KEY_FIND();
    }

    if((iRc=IniFile_Iterator_KeyGetValue(tagFile,
                                         &tagCfgData)) != INI_ERR_NONE)
    {
      ERR_INI_GET_KEY_VALUE();
    }
    lTmp=strtol(caTmp,&pcTmp,10);
    if((*pcTmp != ',') ||
       (lTmp < 0) ||
       (lTmp > UCHAR_MAX)) /* If Greater than UCHAR_MAX, failure */
    {
      ERR_PRINTF("conversion failed (Fanspeed) for value \"%s\", Correct format: FanSpeedX=<speed in %%>,<temperature in 1/10 °C>",
                caTmp);
      IniFile_Dispose(tagFile);
      return(1);
    }
    tagaTemps[uiIndex].ucFanSpeedPercent=(unsigned char)lTmp;

    ++pcTmp; /* Skip ',' */
    lTmp=strtol(pcTmp,&pcTmp,10);
    if((*pcTmp != '\0') ||
       (lTmp < 0) ||
       (lTmp > INT_MAX)) /* If Greater than INT_MAX, failure */
    {
      ERR_PRINTF("conversion failed (Temperature) for value \"%s\", Correct format: FanSpeedX=<speed in %%>,<temperature in 1/10 °C>",
                caTmp);
      IniFile_Dispose(tagFile);
      return(1);
    }
    tagaTemps[uiIndex].iTemp=(int)lTmp;
  }
  if(uiIndex < 2) /* Check if enough temperature points where found */
  {
    ERR_PUTS("Too less Temperature Points found in config file, minimum=2");

    IniFile_Dispose(tagFile);
    return(1);
  }
  *puiTempsCount=uiIndex;

  IniFile_Dispose(tagFile);
  return(0);
}
