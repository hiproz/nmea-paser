
// clang-format off
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "nmea.h"
// clang-format on
///////////////////////////////////////////////////////////////////////////
nmea_parsed_struct gps = {0};

char rmc_str[MAX_NMEA_LINE_LEN];
char nmea[MAX_NMEA_LINE_LEN] = {0};
#if SEPERATE_BD == 1
char NMEA_KEY[][6] = {"GNRMC", "GPVTG", "GNGGA", "GPGSA", "GPGSV", "GPGLL"};
#else
char NMEA_KEY[][6] = {"GPRMC", "GPVTG", "GPGGA", "GPGSA", "GPGSV", "GPGLL"};
#endif
U8 loc_success_times = 0; //连续定位成功的次数
#define MIN_LOC_SUCCESS_TIMES 5
/////////////////////////////////////////////////////////////////////////////
int parse_nmea(nmea_parsed_struct *gps, NMEA_TYPE_EN key, char *buf);
int parse_rmc(nmea_parsed_struct *gps, char *buf);
int parse_gga(nmea_parsed_struct *gps, char *buf);
int parse_gsv(nmea_parsed_struct *gps, char *buf);
////////////////////////////////////////////////////////////////////////////
int get_nmea(char *buf, U8 key, char *nmea, U8 len) {
  char *head, *tail;
  U8 data_len = len;
  int i;

  if (buf == NULL || nmea == NULL)
    return ERROR;

  // test
  memset(nmea, 0, MAX_NMEA_LINE_LEN);
  // strcpy(nmea,
  //       "GNRMC,161311.000,A,2234.8802,N,51.3389,E,0.09,316.36,271117,,,D*78");
  // strcpy(buf, "$GNRMC,092846.400,A,3029.7317,N,10404.1784,E,000.0,183.8,"
  //              "070417,,,A*73\r\n");

  // log(INF,"GNSS buf %u:\n%s\n", strlen((char *)buf), buf);
  head = strstr((char *)buf, (const char *)NMEA_KEY[key]);
  if (head == NULL || head >= buf + strlen((char *)buf)) {
    // printf(RUN, "can't find %s\n", NMEA_KEY[key]);
    return ERROR;
  }

  // log(INF,"GNRMC:%s\r\n", head);
  tail = strstr((char *)head, "\r\n");
  if (tail == NULL || tail >= buf + strlen((char *)buf)) {
    log(INF, "can't find 0x0A0D\r\n");

    return ERROR;
  }

  if (tail - head < len)
    data_len = tail - head;

  strncpy(nmea, head, data_len);
  log(INF, "%s,len %u: %s\r\n", NMEA_KEY[key], data_len, nmea);
  // xor checksum
  head = strchr((char *)nmea, '*');
  if (head == NULL) {
    log(INF, "nmea format error\r\n");
    return ERROR;
  }
  int nmea_len = head - nmea;
  // log(INF,"nmea len:%d\r\n", nmea_len);

  int checksum_read = 0;
  head++;
  if (*head >= 0x30 && *head <= 0x39)
    checksum_read += (*head - 0x30) * 16;
  else if (*head >= 0x41 && *head <= 0x5A)
    checksum_read += (*head - 0x41 + 10) * 16;
  else {
    log(INF, "checksum error\r\n");
    return ERROR;
  }
  head++;
  if (*head >= 0x30 && *head <= 0x39)
    checksum_read += (*head - 0x30);
  else if (*head >= 0x41 && *head <= 0x5A)
    checksum_read += (*head - 0x41 + 10);
  else {
    log(INF, "checksum error\r\n");
    return ERROR;
  }
  // log(INF,"read checksum 0X%X\r\n", checksum_read);

  int checksum_calc = 0;
  for (i = 0; i < nmea_len; i++)
    checksum_calc ^= nmea[i];
  // log(INF,"calc checksum 0X%X\r\n", checksum_calc);

  if (checksum_read == checksum_calc) {
    return SUCCESS;
  } else {
    log(INF, "checksum error\r\n");
    return ERROR;
  }

  return SUCCESS;
}

int get_gps_info(char *buf) {
  S32 ret = ERROR;
  U16 offset = 0;

  // GGA
  while (1) {
    memset(nmea, 0, MAX_NMEA_LINE_LEN);
    ret = get_nmea((char *)buf + offset, EN_GGA, nmea, MAX_NMEA_LINE_LEN - 1);
    if (ret != SUCCESS) {
      log(INF, "Get GGA failed!\n");
      break;
    } else {
      // printf("GGA:%s\n", nmea);
      ret = parse_nmea(&gps, EN_GGA, nmea);
      if (ret != SUCCESS) {
        gps.sat_uesed = 0;
        log(INF, "Parse GGA failed!\n");
      }
      offset += strlen(nmea);
    }
  }

  // GSV
  offset = 0;
  while (1) {
    memset(nmea, 0, MAX_NMEA_LINE_LEN);
    ret = get_nmea((char *)buf + offset, EN_GSV, nmea, MAX_NMEA_LINE_LEN - 1);
    if (ret != SUCCESS) {
      log(INF, "Get GSV failed!\n");
      break;
    } else {
      printf("GSV:%s\n", nmea);
      ret = parse_nmea(&gps, EN_GSV, nmea);
      if (ret != SUCCESS) {
        gps.sat_uesed = 0;
        log(INF, "Parse GSV failed!\n");
      }
      offset += strlen(nmea);
    }
  }

  // RMC
  offset = 0;
  while (1) {
    memset(nmea, 0, MAX_NMEA_LINE_LEN);
    ret = get_nmea((char *)buf + offset, EN_RMC, nmea, MAX_NMEA_LINE_LEN - 1);
    if (ret != SUCCESS) {
      log(INF, "Get RMC failed!\r\n");
      break;
    } else {
      // printf("RMC:%s\n", nmea);
      memset(rmc_str, 0, sizeof(rmc_str));
      strcpy(rmc_str, nmea);
      ret = parse_nmea(&gps, EN_RMC, nmea);
      if (ret != SUCCESS) {
// 大牲畜清零，头羊网关不清零，使用前一个位置，尽可能保证小羊的上线
#if DEVICE_TYPE == 1
        gps.state = EN_GS_V;
        gps.latitude = 0;
        gps.longitude = 0;
        memset(gps.lat_str, 0, sizeof(gps.lat_str));
        memset(gps.lng_str, 0, sizeof(gps.lng_str));
#endif
#if GPS_SIMU_SWITCH == 1
        gps.latitude = 40647191;
        gps.longitude = 204938407;
        gps.lat_ind_str[0] = 'N';
        gps.lng_ind_str[0] = 'E';
        memset(gps.lat_str, 0, sizeof(gps.lat_str));
        strcpy(gps.lat_str, "2234.906386");

        memset(gps.lng_str, 0, sizeof(gps.lng_str));
        strcpy(gps.lng_str, "11351.280253");
        ret = SUCCESS;
        return ret;
#endif
        log(INF, "Parse RMC failed!\r\n");
        loc_success_times = 0;
        ret = ERROR;
      }

      if (gps.latitude > 0 || gps.longitude > 0) {
        log(RUN, "RMC success %u times.lat:%s;lng:%s;speed:%u;sat used:%u\r\n",
            loc_success_times, gps.lat_str, gps.lng_str, gps.speed,
            gps.sat_uesed);
        loc_success_times++;
        if (loc_success_times >= MIN_LOC_SUCCESS_TIMES) {
          loc_success_times = 0;
          return SUCCESS;
        }
      } else {
        ret = ERROR;
      }
      offset += strlen(nmea);
    }
  }
  return ret;
}

int parse_nmea(nmea_parsed_struct *gps, NMEA_TYPE_EN key, char *buf) {
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
int parse_rmc(nmea_parsed_struct *gps, char *buf) {
  // int ret;
  char *head, *tail;
  double dvalue;
  U32 uvalue;
  char tmp[16] = {0};

  if (gps == NULL || buf == NULL)
    return ERROR;

  // GPRMC,133538.12,A,4717.13792,N,00834.16028,E,13.795,111.39,190203,,,A*53
  // GPRMC,,V,,,,,,,,,,N*53
  // GPRMC,091135.00,A,2232.10968,N,11401.40711,E,0.302,,311014,,,A*79
  // log(INF,"GXRMC:%s\n", buf);
  // 1. 时间
  head = buf + 6;
  tail = strchr((char *)head, ',');
  if (tail == NULL) {
    log(INF, "invalid format:%s\r\n", head);
    return ERROR;
  }

  //根据时间决定定位信息是否有效
  //说明2个逗号相邻，数据为空
  if (tail - head == 0 || tail - head > 16) {
    log(INF, "invalid gprmc,find datetime failed\r\n");
    return ERROR;
  }

  memcpy(tmp, head, tail - head);
  // log(INF,"gps datetiem:%s\n", tmp);
  dvalue = atof(tmp);
  uvalue = (U32)dvalue;
  gps->datetime[3] = uvalue / 10000;
  gps->datetime[4] = (uvalue % 10000) / 100;
  gps->datetime[5] = uvalue % 100;
  // log(INF,"time: %d %d %d\n", gps->datetime[3],
  // gps->datetime[4],gps->datetime[5]);

  // 定位状态
  head = tail + 1;

  if (*head == 'A') {
    gps->state = EN_GS_A;
  } else // if(*head == 'V')
  {
    gps->state = EN_GS_V;
    log(INF, "GPRMC state : V, invalid location\r\n");
    return ERROR;
  }

  tail = head + 1;
  // 3. 纬度
  head = tail + 1;
  tail = strchr((char *)head, ',');
  if (tail == NULL) {
    log(INF, "invalid format:%s\r\n", head);
    return ERROR;
  }
  //说明2个逗号相邻，数据为空
  if (tail - head == 0 || tail - head > 16) {
    log(INF, "invalid gprmc, find lat failed\r\n");
    return ERROR;
  }
  memset(tmp, 0, 16);
  memcpy(tmp, head, tail - head);
  memset(gps->lat_str, 0, sizeof(gps->lat_str));
  strcpy(gps->lat_str, tmp);

  dvalue = atof(tmp);
  uvalue = (U32)dvalue;
  gps->latitude =
      (U32)(((uvalue / 100) * 60 + (dvalue - uvalue / 100 * 100)) * 30000);

  // dvalue = uvalue / 100 + (dvalue - uvalue / 100) / 60;
  // memset(tmp, 0, 16);
  // sprintf(tmp, "%f", dvalue);
  // strcpy(gps->lat_str, tmp);
  // log(INF,"gps lat %s\n", gps->lat_str);
  // 4.纬度属性
  head = tail + 1;

  if (*head == 'N') {
    gps->lat_ind = EN_NORTH;
    gps->lat_ind_str[0] = 'N';
    gps->lat_ind_str[1] = 0;
    tail = head + 1;
  } else if (*head == 'S') {
    gps->lat_ind = EN_SOUTH;
    gps->lat_ind_str[0] = 'S';
    gps->lat_ind_str[1] = 0;
    tail = head + 1;
  } else {
    log(INF, "invalid lng indication\r\n");
    return ERROR;
  }
  // 5. 经度
  head = tail + 1;
  tail = strchr((char *)head, ',');
  if (tail == NULL) {
    log(INF, "invalid format:%s\r\n", head);
    return ERROR;
  }
  if (tail - head == 0 || tail - head > 16) {
    log(INF, "invalid gprmc,find lng failed\r\n");
    return ERROR;
  }
  memset(tmp, 0, 16);
  memcpy(tmp, head, tail - head);
  memset(gps->lng_str, 0, sizeof(gps->lng_str));
  strcpy(gps->lng_str, tmp);

  dvalue = atof(tmp);
  uvalue = (U32)dvalue;
  gps->longitude =
      (U32)((uvalue / 100 * 60 + (dvalue - uvalue / 100 * 100)) * 30000);

  // dvalue = uvalue / 100 + (dvalue - uvalue / 100) / 60;
  // memset(tmp, 0, 16);
  // sprintf(tmp, "%f", dvalue);
  // strcpy(gps->lng_str, tmp);
  // log(INF,"gps lng %s\n", gps->lng_str);
  // 6.经度属性
  head = tail + 1;

  if (*head == 'E') {
    gps->long_ind = EN_EAST;
    gps->lng_ind_str[0] = 'E';
    gps->lng_ind_str[1] = 0;
    tail = head + 1;
  } else if (*head == 'W') {
    gps->long_ind = EN_WEST;
    gps->lng_ind_str[0] = 'W';
    gps->lng_ind_str[1] = 0;
    tail = head + 1;
  } else {
    log(INF, "invalid lat indication\r\n");
    return ERROR;
  }

  /*GPRMC,133538.12,A,4717.13792,N,00834.16028,E,13.795,111.39,190203,,,A*53*/
  // 7. 速度
  head = tail + 1;
  tail = strchr((char *)head, ',');
  if (tail == NULL) {
    log(INF, "invalid format:%s\r\n", head);
    return ERROR;
  }
  if (tail - head == 0 || tail - head > 16) {
    log(INF, "invalid packet\r\n");
    return ERROR;
  }
  memset(tmp, 0, 16);
  memcpy(tmp, head, tail - head);
  dvalue = atof(tmp);
  // knots 转换成kph
  gps->speed = (U16)(dvalue * 1.852) * 100;

  // 8. 航向 以真北为参考
  head = tail + 1;
  tail = strchr((char *)head, ',');
  if (tail == NULL) {
    log(INF, "invalid format:%s\r\n", head);
    return ERROR;
  }
  if (tail - head == 0 || tail - head > 16) {
    log(INF, "invalid packet\r\n");
    return ERROR;
  }
  memset(tmp, 0, 16);
  memcpy(tmp, head, tail - head);
  dvalue = atof(tmp);
  gps->course = (U16)dvalue * 100;
  // 9. 日期
  head = tail + 1;
  tail = strchr((char *)head, ',');
  if (tail == NULL) {
    log(INF, "invalid format:%s\r\n", head);
    return ERROR;
  }
  if (tail - head == 0 || tail - head > 16) {
    log(INF, "invalid packet\r\n");
    return ERROR;
  }
  memcpy(tmp, head, tail - head);
  uvalue = (U32)atof(tmp);
  gps->datetime[2] = uvalue / 10000;
  gps->datetime[1] = (uvalue % 10000) / 100;
  gps->datetime[0] = uvalue % 100;

  // log(INF,"datatime: %d %d %d %d %d %d\n", gps->datetime[0],
  // gps->datetime[1],
  //        gps->datetime[2], gps->datetime[3], gps->datetime[4],
  //        gps->datetime[5]);

  // 10 磁偏角
  head = tail + 1;
  tail = strchr((char *)head, ',');
  if (tail == NULL) {
    log(INF, "invalid format:%s\r\n", head);
    return ERROR;
  }
  // bugfix: 磁偏角可能为空 无需强制判断长度
  memset(tmp, 0, 16);
  memcpy(tmp, head, tail - head);
  dvalue = atof(tmp);
  gps->magnetic_value = (U16)dvalue * 100;

  // 11. 磁偏角方向
  head = tail + 1;

  if (*head == 'E') {
    gps->magnetic_ind = EN_EAST;
    tail = head + 1;
  } else if (*head == 'W') {
    gps->magnetic_ind = EN_WEST;
    tail = head + 1;
  } else {
    gps->magnetic_ind = EN_INV;
    tail = head;
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
    log(INF, "GPRMC mode : N, invalid location\r\n");
    return ERROR;
  }

  return SUCCESS;
}

int parse_gga(nmea_parsed_struct *gps, char *buf) {
  char *head, *tail;
  double dvalue;
  char tmp[16] = {0};
  U8 i;

  if (gps == NULL || buf == NULL)
    return ERROR;

  /*GPGGA,133404.00,4717.14117,N,00833.86174,E,1,11,1.49,478.7,M,48.0,M,,0*62*/
  /*GPGGA,,,,,,0,00,99.99,,,,,,*48*/
  // log(INF,"GXGGA:%s\n", buf);
  // 1. 时间
  head = buf + 6;

  for (i = 0; i < 6; i++) {
    tail = strchr(head, ',');
    if (tail == NULL) {
      log(INF, "invalid format:%s\r\n", head);
      return ERROR;
    }
    head = tail + 1;
  }

  // 2. 卫星个数
  tail = strchr(head, ',');
  if (tail == NULL) {
    log(INF, "invalid format:%s\r\n", head);
    return ERROR;
  }
  if (tail - head == 0 || tail - head > 16) {
    log(INF, "invalid packet\r\n");
    return ERROR;
  }
  memcpy(tmp, head, tail - head);
  gps->sat_uesed = atoi(tmp);

  if (gps->sat_uesed == 0) {
    log(INF, "sat num: 0;invalid gpgga\r\n");
    return ERROR;
  }

  // 2. HDOP
  head = tail + 1;
  tail = strchr(head, ',');
  if (tail == NULL) {
    log(INF, "invalid format:%s\r\n", head);
    return ERROR;
  }
  if (tail - head == 0 || tail - head > 16) {
    log(INF, "invalid packet\r\n");
    return ERROR;
  }
  memset(tmp, 0, 16);
  memcpy(tmp, head, tail - head);
  dvalue = atof(tmp);
  gps->hdop = (U16)dvalue * 100;

  // 3. 海拔
  head = tail + 1;
  tail = strchr(head, ',');
  if (tail == NULL) {
    log(INF, "invalid format:%s\r\n", head);
    return ERROR;
  }
  if (tail - head == 0 || tail - head > 16) {
    log(INF, "invalid packet\r\n");
    return ERROR;
  }
  memset(tmp, 0, 16);
  memcpy(tmp, head, tail - head);
  dvalue = atof(tmp);
  gps->msl_altitude = (U16)dvalue * 10;

  // print the gps info
  // printf(RUN, "gps info: sat num %u; HDOP %u; msl_altitude %u\n",
  // gps->sat_uesed, gps->hdop, gps->msl_altitude);

  return SUCCESS;
}
//$GPGSV,5,1,17,01,71,048,31,07,58,211,33,30,58,274,,195,57,046,19*40
//$GPGSV,5,2,17,11,57,025,,194,52,155,32,50,46,122,28,18,44,040,18*4E
//$GPGSV,5,3,17,28,37,332,,193,30,133,34,22,29,110,34,03,25,137,39*4C
//$GPGSV,5,4,17,17,23,276,,08,22,050,30,19,06,260,,09,03,196,30*79
//$GPGSV,5,5,17,06,01,216,*4D

int snr_list_add(snr_node_struct *node) {
  int i;
  if (gps.snr_index < MAX_SNR_NUM) {
    for (i = 0; i < gps.snr_index; i++) {
      if (gps.snr_list[i].sat_no == node->sat_no) {
        memcpy(&gps.snr_list[i], node, sizeof(snr_node_struct));
        printf("update %u node:%u %u \r\n", i, node->sat_no, node->snr);
        return SUCCESS;
      }
    }
    memcpy(&gps.snr_list[gps.snr_index], node, sizeof(snr_node_struct));
    printf("insert new snr node:%u %u \r\n", node->sat_no, node->snr);
    gps.snr_index++;
    printf("snr num:%u\r\n");
    return SUCCESS;
  } else
    return ERROR;
}
void snr_list_sort(void) {
  int i, j;
  snr_node_struct tmp;

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
int parse_gsv(nmea_parsed_struct *gps, char *buf) {
  char *head, *tail;
  U16 sat_no;
  U8 snr;
  char tmp[16] = {0};
  U8 i;

  if (gps == NULL || buf == NULL)
    return ERROR;
  // printf("1\r\n");

  head = buf + 6;
  for (i = 0; i < 3; i++) {
    // printf("2:%s\r\n", head);
    tail = strchr(head, ',');
    if (tail == NULL) {
      log(INF, "invalid format:%s\r\n", head);
      return ERROR;
    }
    // printf("3:%s\r\n", tail);
    head += (tail + 1 - head);
  }

  // 找到第一组数据
  do {
    // printf("4:%s\r\n", head);
    tail = strchr(head, ',');
    if (tail == NULL) {
      log(INF, "invalid format:%s\r\n", head);
      return ERROR;
    }
    if (tail - head == 0 || tail - head > 16) {
      log(INF, "invalid packet\r\n");
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
        log(INF, "invalid format:%s\r\n", head);
        return ERROR;
      }
      head += (tail + 1 - head);
    }
    // printf("5:%s\r\n", head);
    tail = strchr(head, ',');
    if (tail == NULL) {
      tail = strchr(head, '*');
      // printf("found *\r\n");
    }
    if (tail - head > 16) { // 允许为空
      log(INF, "invalid packet\r\n");
      // printf("invalid len\r\n");
      return ERROR;
    }
    if (tail - head) {
      memset(tmp, 0, sizeof(tmp));
      memcpy(tmp, head, tail - head);
      // printf("6:%s\r\n", tmp);
      snr = atoi(tmp);
    } else
      snr = 0;
    head += (tail + 1 - head);

    // printf("valide node,sat no %u snr val %u\n", sat_no, snr);

    // 存储当前节点解析的出来snr对值
    snr_node_struct snr_node;
    snr_node.sat_no = sat_no;
    snr_node.snr = snr;
    snr_list_add(&snr_node);
  } while (*tail != '*');

  snr_list_sort();

  return SUCCESS;
}
