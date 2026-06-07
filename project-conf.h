#ifndef PROJECT_CONF_H_
#define PROJECT_CONF_H_

//#define LOG_CONF_LEVEL_IPV6                        LOG_LEVEL_DBG
//#define LOG_CONF_LEVEL_RPL                         LOG_LEVEL_DBG
//#define LOG_CONF_LEVEL_TCPIP                       LOG_LEVEL_DBG
//#define LOG_CONF_LEVEL_MAC                         LOG_LEVEL_DBG
//#define LOG_CONF_LEVEL_FRAMER                      LOG_LEVEL_DBG

// Log seviyesini düşürerek ciddi yer kazanırsın
#define LOG_CONF_LEVEL_RPL                         LOG_LEVEL_NONE
#define LOG_CONF_LEVEL_TCPIP                       LOG_LEVEL_NONE
#define LOG_CONF_LEVEL_IPV6                        LOG_LEVEL_NONE
#define LOG_CONF_LEVEL_6LOWPAN                     LOG_LEVEL_NONE

// Coffee için minimum ayarlar
#define RELINE_WITH_COFFEE 1
#define CFS_COFFEE_CONF_LOG_DIVISOR 2

#undef COOJA_CONF_DATA_SIZE
#define COOJA_CONF_DATA_SIZE 200000

#endif /* PROJECT_CONF_H_ */
