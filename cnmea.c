#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "cnmea.h"
////////////////////////////////////////
#define CNMEA_LOG_ENABLE 1
#define CLOG             1
#define FREERTOS         1

#if FREERTOS
#include "FreeRTOS.h"
#include "timers.h"
// delay ms
#define cnmea_delay vTaskDelay
#else
#define cnmea_delay
#endif

#if CNMEA_LOG_ENABLE
#ifdef CLOG
#include "clog.h"
#define cnmea_inf clog_inf
#define cnmea_war clog_war
#define cnmea_err clog_err
#else
#define cnmea_inf printf
#define cnmea_war printf
#define cnmea_err printf
#endif
#else
#define cnmea_inf
#define cnmea_war
#define cnmea_err
#endif  //CNMEA_LOG_ENABLE

///////////////////////////////////////////////////////////////////////////
nmea_parsed_struct gps = {0};

//char rmc_str[MAX_NMEA_LINE_LEN];
char nmea[MAX_NMEA_LINE_LEN] = {0};

char NMEA_KEY[][6] = {"RMC", "VTG", "GGA", "GSA", "GSV", "GLL"};

uint8_t loc_success_times = 0;  //连续定位成功的次数
#define MIN_LOC_SUCCESS_TIMES 15
/////////////////////////////////////////////////////////////////////////////
int parse_nmea(nmea_parsed_struct* gps, NMEA_TYPE_EN key, char* buf);
int parse_rmc(nmea_parsed_struct* gps, char* buf);
int parse_gga(nmea_parsed_struct* gps, char* buf);
int parse_gsv(nmea_parsed_struct* gps, char* buf);
////////////////////////////////////////////////////////////////////////////

int get_nmea(char** nmea_buf, uint8_t key, char* nmea, uint8_t len) {
  char *  head, *tail;
  uint8_t data_len = len;
  int     i;
  char*   buf = *nmea_buf;

  if (buf == NULL || nmea == NULL)
    return ERROR;

  memset(nmea, 0, MAX_NMEA_LINE_LEN);

  // 适配北斗和GPS，所以跳过前3个字符
  head = strstr((char*)buf + 3, (const char*)NMEA_KEY[key]);
  if (head == NULL || head >= buf + strlen((char*)buf)) {
    //cnmea_war( "can't find %s\n", NMEA_KEY[key]);
    return ERROR;
  } else {
    cnmea_inf("found %s\r\n", NMEA_KEY[key]);
  }

  // cnmea_inf("GNRMC:%s\r\n", head);
  tail = strstr((char*)head, "\r\n");
  if (tail == NULL || tail >= buf + strlen((char*)buf)) {
    cnmea_inf("can't find 0x0A0D\r\n");

    return ERROR;
  }

  // 不包含$,所以是2
  // 这里就是获取不含$的整条报文
  if (tail - (head - 2) < len)
    data_len = tail - (head - 2);
#if GPS_SIMU_SWITCH == 0
  strncpy(nmea, (head - 2), data_len);
#else
  // 经纬度转换：
  // http://www.gzhatu.com/du2dfm.html
  // 120.60132208247236，46.345724630462854 =>
  // 120.6609459335181， 46.31641876415422 =>  12039.6567 4618.985125

  // 模拟经纬度
  srand(hal_time.ui32Second);
  // 不能带 $
  sprintf(nmea, "GPRMC,092846.400,A,4618.98%02d,N,12039.65%02d,E,000.0,183.8,070417,,,A*73", rand() % 100,
          rand() % 100);
#endif

  //更新搜索的起点，准备下次继续搜索
  *nmea_buf = tail + 2;

  //cnmea_inf( "%s,len %u: %s\r\n", NMEA_KEY[key], data_len, nmea);
  // xor checksum
  head = strchr((char*)nmea, '*');
  if (head == NULL) {
    cnmea_err("nmea format error:%s\r\n", nmea);
    return ERROR;
  }

  //校验内容的长度
  int nmea_len = head - nmea;
  // cnmea_inf("nmea len:%d\r\n", nmea_len);

  int checksum_read = 0;
  head++;
  if (*head >= 0x30 && *head <= 0x39)
    checksum_read += (*head - 0x30) * 16;
  else if (*head >= 0x41 && *head <= 0x5A)
    checksum_read += (*head - 0x41 + 10) * 16;
  else {
    cnmea_err("checksum error\r\n");
    return ERROR;
  }
  head++;
  if (*head >= 0x30 && *head <= 0x39)
    checksum_read += (*head - 0x30);
  else if (*head >= 0x41 && *head <= 0x5A)
    checksum_read += (*head - 0x41 + 10);
  else {
    cnmea_err("checksum error\r\n");
    return ERROR;
  }
  // cnmea_inf("read checksum 0X%X\r\n", checksum_read);

  int checksum_calc = 0;
  for (i = 0; i < nmea_len; i++) {
    checksum_calc ^= nmea[i];
  }

  cnmea_inf("%s,len %u: %s\r\n", NMEA_KEY[key], data_len, nmea);
  //cnmea_inf( "calc checksum 0X%X\r\n", checksum_calc);

#if GPS_SIMU_SWITCH == 0
  if (checksum_read == checksum_calc)
#else
  if (1)
#endif
  {
    return SUCCESS;
  } else {
    cnmea_err("checksum error，read:%d, calc:%d\r\n", checksum_read, checksum_calc);
    return ERROR;
  }
}
/**
 *
 * 注意: 这个接口的buf必须包含完整的类似“$GNRMC”的报文头部特征，否则解析可能失效
 */
int get_gps_info(char* buf, uint16_t len) {
  int   ret       = ERROR;
  char* buf_parse = NULL;

  buf[len - 1] = 0;

  //nmea 的特征头 $GXRMC,
  if (len < 7) {
    cnmea_err("invalid len of NMEA\r\n");
    return ERROR;
  }

  buf_parse = buf;

  // GGA
  while (*buf_parse) {
    memset(nmea, 0, MAX_NMEA_LINE_LEN);
    ret = get_nmea((char**)&buf_parse, EN_GGA, nmea, MAX_NMEA_LINE_LEN - 1);
    if (ret != SUCCESS) {
      break;
    } else {
      ret = parse_nmea(&gps, EN_GGA, nmea);
      if (ret != SUCCESS) {
        cnmea_inf("parse GGA failed!\r\n");
        cnmea_delay(50);
        continue;
      } else {
        break;
      }
    }
  }

  // GSV
  buf_parse = buf;
  while (*buf_parse) {
    int index = 0;
    memset(nmea, 0, MAX_NMEA_LINE_LEN);
    ret = get_nmea((char**)&buf_parse, EN_GSV, nmea, MAX_NMEA_LINE_LEN - 1);
    if (ret != SUCCESS) {
      break;
    } else {
      // 支持多组报文
      cnmea_inf("gsv %d:%s\r\n", index, nmea);
      index++;
      parse_nmea(&gps, EN_GSV, nmea);
    }
  }

  // RMC
  buf_parse = buf;
  while (*buf_parse) {
    memset(nmea, 0, MAX_NMEA_LINE_LEN);
    ret = get_nmea((char**)&buf_parse, EN_RMC, nmea, MAX_NMEA_LINE_LEN - 1);
    if (ret != SUCCESS) {
      break;
    } else {
      //memset(rmc_str, 0, sizeof(rmc_str));
      //strcpy(rmc_str, nmea);
      ret = parse_nmea(&gps, EN_RMC, nmea);
      if (ret != SUCCESS) {
        cnmea_inf("parse RMC failed!\r\n");
        cnmea_delay(50);
        continue;
      }

      if (gps.latitude > 0 || gps.longitude > 0) {
        gps.gps_average.lat[gps.gps_average.index] = gps.f_latitude;
        gps.gps_average.lng[gps.gps_average.index] = gps.f_longitude;
        gps.gps_average.index                      = (gps.gps_average.index + 1) % GPS_ARVRG_NUM;
        gps.gps_average.count++;
        if (gps.gps_average.count > GPS_ARVRG_NUM) {
          gps.gps_average.count = GPS_ARVRG_NUM;
        }
        loc_success_times++;
        cnmea_inf("RMC success %u times.lat:%s;lng:%s;speed:%u;sat used:%u\r\n", loc_success_times, gps.lat_str,
                  gps.lng_str, gps.speed, gps.sat_uesed);
        if (loc_success_times >= MIN_LOC_SUCCESS_TIMES) {
          loc_success_times = 0;
          // 求平均值
          int   i;
          float lat_sum = 0;
          float lng_sum = 0;
          for (i = 0; i < gps.gps_average.count; i++) {
            lat_sum += gps.gps_average.lat[i];
            lng_sum += gps.gps_average.lng[i];
          }

          gps.gps_average.lat_average = lat_sum / gps.gps_average.count;
          gps.gps_average.lng_average = lng_sum / gps.gps_average.count;
          return SUCCESS;
        } else {
          cnmea_delay(50);
          continue;
        }
      } else {
        loc_success_times = 0;
        cnmea_inf("locating failed, invalid location\r\n");
        cnmea_delay(50);
        continue;
      }
    }
  }

  gps.state     = EN_GS_V;
  gps.latitude  = 0;
  gps.longitude = 0;
  memset(gps.lat_str, 0, sizeof(gps.lat_str));
  memset(gps.lng_str, 0, sizeof(gps.lng_str));

  cnmea_war("gps success %d times, less than %d times,locating continue\r\n", loc_success_times, MIN_LOC_SUCCESS_TIMES);
  return ret;
}

int parse_nmea(nmea_parsed_struct* gps, NMEA_TYPE_EN key, char* buf) {
  int ret = ERROR;

  if (gps == NULL || buf == NULL)
    return ERROR;

  switch (key) {
  case EN_RMC:
    ret = parse_rmc(gps, buf);
    break;
  case EN_GGA:
    ret = parse_gga(gps, buf);
    break;
  case EN_GSV:
    ret = parse_gsv(gps, buf);
    break;
  default:
    ret = ERROR;
    break;
  }

  return ret;
}
int parse_rmc(nmea_parsed_struct* gps, char* buf) {
  // int ret;
  char *   head, *tail;
  double   dvalue;
  uint32_t uvalue;
  char     tmp[16] = {0};

  if (gps == NULL || buf == NULL)
    return ERROR;

  // GPRMC,133538.12,A,4717.13792,N,00834.16028,E,13.795,111.39,190203,,,A*53
  // GPRMC,,V,,,,,,,,,,N*53
  // GPRMC,091135.00,A,2232.10968,N,11401.40711,E,0.302,,311014,,,A*79
  // cnmea_inf("RMC:%s\n", buf);
  // 1. 时间
  // 从第一个逗号后面开始
  head = buf + 6;
  tail = strchr((char*)head, ',');
  if (tail == NULL) {
    cnmea_inf("invalid format:%s\r\n", head);
    return ERROR;
  }

  //根据时间决定定位信息是否有效
  //说明2个逗号相邻，数据为空
  if (tail - head == 0 || tail - head > 16) {
    cnmea_inf("invalid gprmc,find datetime failed\r\n");
    return ERROR;
  }

  memcpy(tmp, head, tail - head);
  // cnmea_inf("gps datetiem:%s\n", tmp);
  dvalue           = atof(tmp);
  uvalue           = (uint32_t)dvalue;
  gps->datetime[3] = uvalue / 10000;
  gps->datetime[4] = (uvalue % 10000) / 100;
  gps->datetime[5] = uvalue % 100;
  // cnmea_inf("time: %d %d %d\n", gps->datetime[3],
  // gps->datetime[4],gps->datetime[5]);

  // 定位状态
  head = tail + 1;

  if (*head == 'A') {
    gps->state = EN_GS_A;
  } else  // if(*head == 'V')
  {
    gps->state = EN_GS_V;
    cnmea_inf("GPRMC state : V, invalid location\r\n");
    return ERROR;
  }

  tail = head + 1;
  // 3. 纬度
  head = tail + 1;
  tail = strchr((char*)head, ',');
  if (tail == NULL) {
    cnmea_inf("invalid format:%s\r\n", head);
    return ERROR;
  }
  //说明2个逗号相邻，数据为空
  if (tail - head == 0 || tail - head > 16) {
    cnmea_inf("invalid gprmc, find lat failed\r\n");
    return ERROR;
  }
  memset(tmp, 0, 16);
  memcpy(tmp, head, tail - head);
  memset(gps->lat_str, 0, sizeof(gps->lat_str));
  strcpy(gps->lat_str, tmp);

  dvalue = atof(tmp);

  uvalue        = (uint32_t)dvalue;
  gps->latitude = (uint32_t)(((uvalue / 100) * 60 + (dvalue - uvalue / 100 * 100)) * 30000);

  gps->f_latitude = (float)gps->latitude / (60 * 30000);

  // dvalue = uvalue / 100 + (dvalue - uvalue / 100) / 60;
  // memset(tmp, 0, 16);
  // sprintf(tmp, "%f", dvalue);
  // strcpy(gps->lat_str, tmp);
  // cnmea_inf("gps lat %s\n", gps->lat_str);
  // 4.纬度属性
  head = tail + 1;

  if (*head == 'N') {
    gps->lat_ind        = EN_NORTH;
    gps->lat_ind_str[0] = 'N';
    gps->lat_ind_str[1] = 0;
    tail                = head + 1;
  } else if (*head == 'S') {
    gps->lat_ind        = EN_SOUTH;
    gps->lat_ind_str[0] = 'S';
    gps->lat_ind_str[1] = 0;
    tail                = head + 1;

    /* 南纬用负数表示 */
    sprintf(gps->lat_str, "-%s", gps->lat_str);
    gps->f_latitude = -(gps->f_latitude);

  } else {
    cnmea_inf("invalid lng indication\r\n");
    return ERROR;
  }
  // 5. 经度
  head = tail + 1;
  tail = strchr((char*)head, ',');
  if (tail == NULL) {
    cnmea_inf("invalid format:%s\r\n", head);
    return ERROR;
  }
  if (tail - head == 0 || tail - head > 16) {
    cnmea_inf("invalid gprmc,find lng failed\r\n");
    return ERROR;
  }
  memset(tmp, 0, 16);
  memcpy(tmp, head, tail - head);
  memset(gps->lng_str, 0, sizeof(gps->lng_str));
  strcpy(gps->lng_str, tmp);

  dvalue = atof(tmp);

  uvalue         = (uint32_t)dvalue;
  gps->longitude = (uint32_t)((uvalue / 100 * 60 + (dvalue - uvalue / 100 * 100)) * 30000);

  gps->f_longitude = (float)gps->longitude / (60 * 30000);
  // dvalue = uvalue / 100 + (dvalue - uvalue / 100) / 60;
  // memset(tmp, 0, 16);
  // sprintf(tmp, "%f", dvalue);
  // strcpy(gps->lng_str, tmp);
  // cnmea_inf("gps lng %s\n", gps->lng_str);
  // 6.经度属性
  head = tail + 1;

  if (*head == 'E') {
    gps->long_ind       = EN_EAST;
    gps->lng_ind_str[0] = 'E';
    gps->lng_ind_str[1] = 0;
    tail                = head + 1;
  } else if (*head == 'W') {
    gps->long_ind       = EN_WEST;
    gps->lng_ind_str[0] = 'W';
    gps->lng_ind_str[1] = 0;
    tail                = head + 1;
    /* 西经用负数表示 */
    sprintf(gps->lng_str, "-%s", gps->lng_str);
    gps->f_longitude = -(gps->f_longitude);
  } else {
    cnmea_inf("invalid lat indication\r\n");
    return ERROR;
  }

  /*GPRMC,133538.12,A,4717.13792,N,00834.16028,E,13.795,111.39,190203,,,A*53*/
  // 7. 速度
  head = tail + 1;
  tail = strchr((char*)head, ',');
  if (tail == NULL) {
    cnmea_inf("invalid format:%s\r\n", head);
    return ERROR;
  }
  if (tail - head == 0 || tail - head > 16) {
    cnmea_inf("invalid packet\r\n");
    return ERROR;
  }
  memset(tmp, 0, 16);
  memcpy(tmp, head, tail - head);
  dvalue = atof(tmp);

  // 单位 km/s
  gps->f_speed = dvalue * 1852;
  // knots 转换成kph  10m/s 为单位
  gps->speed = (uint16_t)(dvalue * 1.852) * 100;

  // 8. 航向 以真北为参考
  head = tail + 1;
  tail = strchr((char*)head, ',');
  if (tail == NULL) {
    cnmea_inf("invalid format:%s\r\n", head);
    return ERROR;
  }
  if (tail - head == 0 || tail - head > 16) {
    cnmea_inf("invalid packet\r\n");
    return ERROR;
  }
  memset(tmp, 0, 16);
  memcpy(tmp, head, tail - head);
  dvalue      = atof(tmp);
  gps->course = (uint16_t)dvalue * 100;
  // 9. 日期
  head = tail + 1;
  tail = strchr((char*)head, ',');
  if (tail == NULL) {
    cnmea_inf("invalid format:%s\r\n", head);
    return ERROR;
  }
  if (tail - head == 0 || tail - head > 16) {
    cnmea_inf("invalid packet\r\n");
    return ERROR;
  }
  memcpy(tmp, head, tail - head);
  uvalue           = (uint32_t)atof(tmp);
  gps->datetime[2] = uvalue / 10000;
  gps->datetime[1] = (uvalue % 10000) / 100;
  gps->datetime[0] = uvalue % 100;

  // cnmea_inf("datatime: %d %d %d %d %d %d\n", gps->datetime[0],
  // gps->datetime[1],
  //        gps->datetime[2], gps->datetime[3], gps->datetime[4],
  //        gps->datetime[5]);

  // 10 磁偏角
  head = tail + 1;
  tail = strchr((char*)head, ',');
  if (tail == NULL) {
    cnmea_inf("invalid format:%s\r\n", head);
    return ERROR;
  }
  // bugfix: 磁偏角可能为空 无需强制判断长度
  memset(tmp, 0, 16);
  memcpy(tmp, head, tail - head);
  dvalue              = atof(tmp);
  gps->magnetic_value = (uint16_t)dvalue * 100;

  // 11. 磁偏角方向
  head = tail + 1;

  if (*head == 'E') {
    gps->magnetic_ind = EN_EAST;
    tail              = head + 1;
  } else if (*head == 'W') {
    gps->magnetic_ind = EN_WEST;
    tail              = head + 1;
  } else {
    gps->magnetic_ind = EN_INV;
    tail              = head;
  }
  // 12. 模式
  head = tail + 1;
  if (*head == 'A') {
    gps->mode = EN_GM_A;
  } else if (*head == 'D') {
    gps->mode = EN_GM_D;
  } else if (*head == 'E') {
    gps->mode = EN_GM_E;
  } else {
    gps->mode = EN_GM_N;
    cnmea_inf("GPRMC mode : N, invalid location\r\n");
    return ERROR;
  }

  return SUCCESS;
}

int parse_gga(nmea_parsed_struct* gps, char* buf) {
  char * head, *tail;
  double dvalue;
  char   tmp[16] = {0};
  int    i;

  if (gps == NULL || buf == NULL)
    return ERROR;

  /*GPGGA,133404.00,4717.14117,N,00833.86174,E,1,11,1.49,478.7,M,48.0,M,,0*62*/
  /*GPGGA,,,,,,0,00,99.99,,,,,,*48*/
  // cnmea_inf("GXGGA:%s\n", buf);
  // 1. 时间
  // 从第一个逗号后面开始
  head = buf + 6;

  for (i = 0; i < 6; i++) {
    tail = strchr(head, ',');
    if (tail == NULL) {
      cnmea_inf("invalid format:%s\r\n", head);
      return ERROR;
    }
    head = tail + 1;
  }

  // 2. 卫星个数
  tail = strchr(head, ',');
  if (tail == NULL) {
    cnmea_inf("invalid format:%s\r\n", head);
    return ERROR;
  }
  if (tail - head == 0 || tail - head > 16) {
    cnmea_inf("invalid packet\r\n");
    return ERROR;
  }
  memcpy(tmp, head, tail - head);
  gps->sat_uesed = atoi(tmp);

  if (gps->sat_uesed == 0) {
    cnmea_inf("sat num: 0;invalid gpgga\r\n");
    return ERROR;
  }

  // 2. HDOP
  head = tail + 1;
  tail = strchr(head, ',');
  if (tail == NULL) {
    cnmea_inf("invalid format:%s\r\n", head);
    return ERROR;
  }
  if (tail - head == 0 || tail - head > 16) {
    cnmea_inf("invalid packet\r\n");
    return ERROR;
  }
  memset(tmp, 0, 16);
  memcpy(tmp, head, tail - head);
  dvalue    = atof(tmp);
  gps->hdop = (uint16_t)dvalue * 100;

  // 3. 海拔
  head = tail + 1;
  tail = strchr(head, ',');
  if (tail == NULL) {
    cnmea_inf("invalid format:%s\r\n", head);
    return ERROR;
  }
  if (tail - head == 0 || tail - head > 16) {
    cnmea_inf("invalid packet\r\n");
    return ERROR;
  }
  memset(tmp, 0, 16);
  memcpy(tmp, head, tail - head);
  dvalue            = atof(tmp);
  gps->f_altitude   = dvalue;
  gps->msl_altitude = (uint16_t)dvalue * 10;

  // print the gps info
  // clog_run( "gps info: sat num %u; HDOP %u; msl_altitude %u\n",
  // gps->sat_uesed, gps->hdop, gps->msl_altitude);

  return SUCCESS;
}
//$GPGSV,5,1,17,01,71,048,31,07,58,211,33,30,58,274,,195,57,046,19*40
//$GPGSV,5,2,17,11,57,025,,194,52,155,32,50,46,122,28,18,44,040,18*4E
//$GPGSV,5,3,17,28,37,332,,193,30,133,34,22,29,110,34,03,25,137,39*4C
//$GPGSV,5,4,17,17,23,276,,08,22,050,30,19,06,260,,09,03,196,30*79
//$GPGSV,5,5,17,06,01,216,*4D

int snr_list_add(snr_node_struct* node) {
  int i;
  if (gps.snr_index < MAX_SNR_NUM) {
    for (i = 0; i < gps.snr_index; i++) {
      if (gps.snr_list[i].sat_no == node->sat_no) {
        memcpy(&gps.snr_list[i], node, sizeof(snr_node_struct));
        cnmea_inf("update %u node:%u %u \r\n", i, node->sat_no, node->snr);
        return SUCCESS;
      }
    }
    memcpy(&gps.snr_list[gps.snr_index], node, sizeof(snr_node_struct));
    cnmea_inf("insert new snr node:%u %u \r\n", node->sat_no, node->snr);
    gps.snr_index++;
    cnmea_inf("snr num:%u\r\n", gps.snr_index);
    return SUCCESS;
  } else
    return ERROR;
}

void snr_list_sort(void) {
  int             i, j;
  snr_node_struct tmp = {0};

  for (i = 0; i < gps.snr_index - 1; i++) {
    for (j = i + 1; j < gps.snr_index; j++) {
      if (gps.snr_list[i].snr < gps.snr_list[j].snr) {
        memcpy(&tmp, &gps.snr_list[i], sizeof(snr_node_struct));
        memcpy(&gps.snr_list[i], &gps.snr_list[j], sizeof(snr_node_struct));
        memcpy(&gps.snr_list[j], &tmp, sizeof(snr_node_struct));
      }
    }
  }
}

int parse_gsv(nmea_parsed_struct* gps, char* buf) {
  char *   head, *tail;
  uint16_t sat_no;
  uint8_t  snr;
  char     tmp[16] = {0};
  int      i;

  if (gps == NULL || buf == NULL)
    return ERROR;

  // 从第一个逗号后面开始
  head = buf + 6;
  for (i = 0; i < 3; i++) {
    tail = strchr(head, ',');
    if (tail == NULL) {
      cnmea_inf("invalid format:%s\r\n", head);
      return ERROR;
    }
    head += (tail + 1 - head);
  }

  // 找到第一组数据
  // 兼容NMEA0183 V4.1
  while (*head != '*' && *(head + 1) != '*') {
    tail = strchr(head, ',');
    if (tail == NULL) {
      cnmea_inf("invalid format:%s\r\n", head);
      return ERROR;
    }
    if (tail - head == 0 || tail - head > 16) {
      cnmea_inf("invalid packet\r\n");
      return ERROR;
    }
    memset(tmp, 0, sizeof(tmp));
    memcpy(tmp, head, tail - head);
    sat_no = atoi(tmp);
    head += (tail + 1 - head);

    // 跳过2个字段
    for (i = 0; i < 2; i++) {
      tail = strchr(head, ',');
      if (tail == NULL) {
        cnmea_inf("invalid format:%s\r\n", head);
        return ERROR;
      }
      head += (tail + 1 - head);
    }
    tail = strchr(head, ',');
    if (tail == NULL) {
      tail = strchr(head, '*');
    }
    if (tail - head > 16) {  // 允许为空
      cnmea_inf("invalid packet\r\n");
      return ERROR;
    }
    if (tail - head) {
      memset(tmp, 0, sizeof(tmp));
      memcpy(tmp, head, tail - head);
      snr = atoi(tmp);
    } else {
      snr = 0;
    }
    head += (tail + 1 - head);

    cnmea_inf("valide node,sat NO. %u, snr val %u\r\n", sat_no, snr);

    // 存储当前节点解析的出来snr对值
    if (snr > 0) {
      snr_node_struct snr_node;
      snr_node.sat_no = sat_no;
      snr_node.snr    = snr;
      snr_list_add(&snr_node);
    }
    cnmea_delay(30);
  }

  snr_list_sort();

  return SUCCESS;
}
