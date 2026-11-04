/*
 * list.h
 *
 * Copyright (c) 2021 - 2022 dora2ios
 *
 * List of supported devices.
 * Currently, source code of payload used with checkra1n is not publicly available.
 * However, checkm8 exploit itself is open source and can be clonedby analyzing
 * USB packets sent by checkra1n.
 *
 */

#ifndef LIST_H
#define LIST_H

/*
 * S5L8960 (Apple A7)
 * S5L8960 is fully supported.
 */
#define S5L8960_CODE
#define S5L8960_PAYLOAD


/*
 * T7000 (Apple A8) // tested on iPhone7,2
 * T7000 is fully supported.
 */
#define T7000_CODE
#define T7000_PAYLOAD


/*
 * T7001 (Apple A8X)
 * T7001 is fully supported.
 */
#define T7001_CODE
#define T7001_PAYLOAD


/*
 * S8000 (Apple A9) // tested on an A9 device
 * S8000 is fully supported.
 */
#define S8000_CODE
#define S8000_PAYLOAD


/*
 * S8003 (Apple A9)
 * S8003 is fully supported. #PR3/5
 */
#define S8003_CODE
#define S8003_PAYLOAD


/*
 * S8001 (Apple A9X)
 * S8001 is fully supported.
 */
#define S8001_CODE
#define S8001_PAYLOAD


/*
 * T8010 (Apple A10 Fusion)
 * T8010 is fully supported.
 */
#define T8010_CODE
#define T8010_PAYLOAD


/*
 * T8011 (Apple A10X Fusion)
 * T8011 is fully supported.
 */
#define T8011_CODE
#define T8011_PAYLOAD

/*
 * T8012 (Apple T2)
 * T8012 is fully supported.
 */
#define T8012_CODE
#define T8012_PAYLOAD

/*
 * T8015 (Apple A11 Bionic)
 * T8015 is fully supported.
 */
#define T8015_CODE
#define T8015_PAYLOAD


#endif
