/* Resource and control IDs for neoFanSpeed */
#ifndef NFS_RESOURCE_H
#define NFS_RESOURCE_H

#define IDI_APP             1

#ifndef IDC_STATIC
#define IDC_STATIC          (-1)
#endif
#define IDR_MENU            100
#define IDR_ACCEL           101

#define IDD_SENSORS         200
#define IDD_FANS            201
#define IDD_LOGGING         202
#define IDD_INFO            203
#define IDD_ALARM           204
#define IDD_DETECT          205

/* menu commands */
#define IDM_LOGTOGGLE       1001
#define IDM_TRAY            1002
#define IDM_EXIT            1003
#define IDM_CELSIUS         1004
#define IDM_FAHRENHEIT      1005
#define IDM_ONTOP           1006
#define IDM_MINI            1007
#define IDM_DETECT          1008
#define IDM_ABOUT           1009
#define IDM_RESET           1010
#define IDM_SHOW            1011
#define IDM_PSILENT         1012
#define IDM_PNORMAL         1013
#define IDM_PFULL           1014
#define IDM_PBIOS           1015
#define IDM_TAB_SENSORS     1020
#define IDM_TAB_FANS        1021
#define IDM_TAB_LOGGING     1022
#define IDM_TAB_INFO        1023

/* main window children */
#define IDC_TAB             1100
#define IDC_STATUS          1101
#define IDC_LOGBTN          1102
#define IDC_RESETBTN        1103
#define IDC_PROFLBL         1104
#define IDC_PROFILE         1105
#define IDC_BANNER          1106
#define IDC_BANNERBTN       1107

/* sensors page */
#define IDC_SLIST           1200
#define IDC_ALARMLBL        1201
#define IDC_LIMIT           1202
#define IDC_LIMITSPIN       1203
#define IDC_WARNWIN         1204
#define IDC_BEEP            1205
#define IDC_BALLOON         1206
#define IDC_GRAPH           1207
#define IDC_SEP1            1208

/* fans page */
#define IDC_FLIST           1300
#define IDC_CTLGRP          1301
#define IDC_MAUTO           1302
#define IDC_MMANUAL         1303
#define IDC_MPROFILE        1304
#define IDC_MCURVE          1305
#define IDC_MANTXT          1306
#define IDC_PCTDN           1307
#define IDC_PCTBAR          1308
#define IDC_PCTUP           1309
#define IDC_PCTVAL          1310
#define IDC_PL20            1311
#define IDC_PL60            1312
#define IDC_PL100           1313
#define IDC_PSILENT         1314
#define IDC_PNORMAL         1315
#define IDC_PFULL           1316
#define IDC_PS1             1317
#define IDC_PS2             1318
#define IDC_PS3             1319
#define IDC_PN1             1320
#define IDC_PN2             1321
#define IDC_PN3             1322
#define IDC_PALL            1323
#define IDC_FOLLOWLBL       1324
#define IDC_FOLLOW          1325
#define IDC_FOLLOWT         1326
#define IDC_HYSTLBL         1327
#define IDC_HYST            1328
#define IDC_HYSTSPIN        1329
#define IDC_CURVE           1330
#define IDC_CMIN            1331
#define IDC_CMAX            1332
#define IDC_CTHDR           1333
#define IDC_CDHDR           1334
#define IDC_PTLBL1          1340  /* 1340..1343 */
#define IDC_PTT1            1344  /* 1344..1347 */
#define IDC_PTTS1           1348  /* 1348..1351 */
#define IDC_PTD1            1352  /* 1352..1355 */
#define IDC_PTDS1           1356  /* 1356..1359 */
#define IDC_AUTOTXT         1360
#define IDC_NCICON          1361
#define IDC_NCTITLE         1362
#define IDC_NCREASON        1363
#define IDC_LED             1364
#define IDC_RSENSOR         1365
#define IDC_RTARGET         1366
#define IDC_RSTATE          1367
#define IDC_WARNLOW         1368
#define IDC_WARNRPM         1369
#define IDC_WARNSPIN        1370
#define IDC_WARNUNIT        1371
#define IDC_BIOSEXIT        1372
#define IDC_FAILSAFE        1373
#define IDC_FLOOR           1374
#define IDC_FLOORSPIN       1375
#define IDC_MSEP            1376

/* logging page */
#define IDC_LOGPATH         1400
#define IDC_BROWSE          1401
#define IDC_FCSV            1402
#define IDC_FTXT            1403
#define IDC_LOGINT          1404
#define IDC_LOGINTSPIN      1405
#define IDC_EXAMPLE         1406
#define IDC_LHEADER         1407
#define IDC_LROLL           1408
#define IDC_LSTART          1409
#define IDC_LVALS           1410
#define IDC_LRECENT         1411
#define IDC_LSUM            1412
#define IDC_LBTN            1413

/* info page */
#define IDC_GPROC           1500
#define IDC_GGFX            1501
#define IDC_GBOARD          1502
#define IDC_GSYS            1503
#define IDC_IDETECT         1504
#define IDC_ICOPY           1505
#define IDC_ISAVE           1506
#define IDC_IMSG            1507
#define IDC_IVAL            1600  /* 1600..1663 runtime labels */

/* alarm dialog */
#define IDC_AICON           1700
#define IDC_ATEXT           1701
#define IDC_ATEXT2          1702
#define IDC_ARESTORE        1703

/* detect dialog */
#define IDC_DTEXT           1800
#define IDC_DL1             1801  /* 1801..1805 */
#define IDC_DR1             1811  /* 1811..1815 */
#define IDC_DPROG           1820
#define IDC_DBOX            1821

#endif
