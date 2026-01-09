
#ifndef GEO_IP_TABLE_H
#define GEO_IP_TABLE_H

#include <stdint.h>

typedef struct
{
	uint32_t		firstAddr;
	uint32_t		lastAddr;
	char			cCode[3];
} GeoIP_gb;

extern GeoIP_gb aGeoIP[];
extern const uint32_t geoIPNumRows;

#endif // GEO_IP_TABLE_H

