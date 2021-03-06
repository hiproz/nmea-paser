#ifndef __NMEA_H__
#define __NMEA_H__
#include <stdint.h> //uintxx_t
#ifdef __cplusplus
extern "C" {
#endif

#ifndef ERROR
#define ERROR 1
#endif
#ifndef SUCCESS
#define SUCCESS 0
#endif
#ifndef BOOL
#define BOOL int
#endif
#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

#ifndef NULL
#define NULL 0
#endif

#define MAX_DATETIME_LEN 6
#define MAX_NMEA_LINE_LEN 256
#define MAX_SNR_NUM 180

#define GPS_ARVRG_NUM 5 //进行gps平均时的位置数
// gps模拟包的开关，为1时，使用代码中的模拟包
#define GPS_SIMU_SWITCH 0

typedef enum { EN_RMC = 0, EN_VTG, EN_GGA, EN_GSA, EN_GSV, EN_GLL } NMEA_TYPE_EN;

typedef enum { EN_GS_A = 0, EN_GS_V, EN_GS_INV } GPS_STATE_EN;

typedef enum { EN_EAST = 0, EN_SOUTH, EN_WEST, EN_NORTH, EN_INV } ORIENTATION_EN;

// 定位类型
typedef enum {

  EN_GM_A,
  EN_GM_D,
  EN_GM_E,
  EN_GM_N,

  EN_GM_INV
} GPS_MODE_EN;

typedef struct {
  int index;
  int count;
  float lat[GPS_ARVRG_NUM];
  float lng[GPS_ARVRG_NUM];
  float lat_average;
  float lng_average;
} gps_average_struct;

typedef struct {
  uint16_t sat_no;
  uint8_t snr;
} snr_node_struct;

typedef struct {
  uint8_t datetime[MAX_DATETIME_LEN];
  uint8_t state;

  // 方便准确值表示，结合ind值使用
  uint32_t latitude;
  uint8_t lat_ind;
  uint32_t longitude;
  uint8_t long_ind;

  // 字符串传输过程避免多次转换
  char lat_str[16];
  char lat_ind_str[2];
  char lng_str[16];
  char lng_ind_str[2];

  uint16_t speed;

  uint16_t course;
  uint16_t magnetic_value;
  uint8_t magnetic_ind;
  uint8_t mode;

  // 方便存储，但是会引入转换误差
  float f_latitude;
  float f_longitude;
  float f_altitude;
  float f_speed;

  uint8_t sat_uesed;
  uint16_t msl_altitude;
  uint16_t hdop;

  uint16_t snr_index;
  snr_node_struct snr_list[MAX_SNR_NUM];

  gps_average_struct gps_average;
} nmea_parsed_struct;

extern nmea_parsed_struct gps;
extern uint8_t loc_success_times;

int get_gps_info(char *buf, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif
